/* This is a basic parser which takes an input source string and produces an
   abstract syntax tree, which will be used to generate the sinistra bytecode.

   Licensed under the MIT License - see LICENSE file for details.
*/


%define api.pure full
%locations
%lex-param {void *scanner}
%parse-param {void *scanner}{SCANNER_STATE_t *state}

%code requires {
  #include <setjmp.h>
  #include <stdbool.h>
  #include <stdint.h>

  #include "compiler/absyn.h"
  #include "compiler/parse_input.h"
  #include "compiler/compdiag.h"

  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    uint64_t maxsize;
  } OUTPUT_t;

  typedef struct {
    int8_t errnum;
    char *errdetail;
    AS_NODE *absyn;
    const char *source_name;
    int line;
    int column;
    int span;
    char *offending_token;
    struct scanner_alloc_s *scanner_allocs;
    jmp_buf scanner_fatal_jmp;
    bool scanner_fatal_jmp_active;
    bool scanner_failed;
    void *scanner_handle;
  } SCANNER_STATE_t;

  int8_t parse_source(const ParseInput *input, AS_NODE **absyn, char **errdetail);
  int8_t parse_source_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, SCANNER_STATE_t *out_state);
  int8_t parse_source_compiler_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, CompilerDiagnostic *diag, SCANNER_STATE_t *out_state);
}

%{
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "parser.h"
#include "memory.h"
#include "libcall.h"

typedef void *yyscan_t;
int yylex (YYSTYPE *yylval_param, YYLTYPE *yylloc_param, yyscan_t yyscanner);
int yylex_init_extra(SCANNER_STATE_t *user_defined, yyscan_t* scanner);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse(void *scanner, SCANNER_STATE_t *state);
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len, yyscan_t yyscanner);
void yy_delete_buffer(YY_BUFFER_STATE b, yyscan_t yyscanner);


static char *parser_strdup(const char *s) {
  const char *source = s ? s : "";
  size_t len = strlen(source);
  size_t size = 0;
  if (alloc_add_overflow(len, 1, &size)) return NULL;
  char *copy = alloc_malloc(size);
  if (!copy) return NULL;
  memcpy(copy, source, size);
  return copy;
}

static void parser_set_failure(SCANNER_STATE_t *state, const char *detail) {
  if (!state || state->errnum != ERR_NOERROR) return;
  state->errnum = ERR_COMP_SYNTAX;
  const char *message = detail ? detail : "parser: out of memory building syntax tree";
  size_t len = strlen(message);
  size_t size = 0;
  if (!alloc_add_overflow(len, 1, &size)) {
    state->errdetail = malloc(size);
    if (state->errdetail) memcpy(state->errdetail, message, size);
  }
}

static AS_NODE *parser_new_value(SCANNER_STATE_t *state, ENUM_VALUE type, char *text) {
  AS_NODE *node = as_new_valnode(type, text);
  if (!node) parser_set_failure(state, NULL);
  return node;
}

static AS_NODE *parser_new_integer(SCANNER_STATE_t *state, char *text) {
  uint64_t value = 0;
  bool valid = text && text[0] != '\0';
  for (const unsigned char *p = (const unsigned char *)text;
       valid && *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      valid = false;
      break;
    }
    uint64_t digit = (uint64_t)(*p - '0');
    if (value > (UINT64_C(9223372036854775807) - digit) / 10u) {
      valid = false;
      break;
    }
    value = value * 10u + digit;
  }
  free(text);
  if (!valid) {
    parser_set_failure(state,
                       "parser: integer literal out of range (expected 0..9223372036854775807)");
    return NULL;
  }
  AS_NODE *node = as_new_intnode((int64_t)value);
  if (!node) parser_set_failure(state, NULL);
  return node;
}

static AS_NODE *parser_new_node(SCANNER_STATE_t *state, ENUM_NODE type,
                                AS_NODE *lhs, AS_NODE *rhs,
                                bool lhs_required, bool rhs_required) {
  if ((lhs_required && !lhs) || (rhs_required && !rhs)) {
    as_delete(lhs);
    as_delete(rhs);
    parser_set_failure(state, NULL);
    return NULL;
  }
  AS_NODE *node = as_new_node(type, lhs, rhs);
  if (!node) {
    as_delete(lhs);
    as_delete(rhs);
    parser_set_failure(state, NULL);
  }
  return node;
}

static AS_IF *parser_new_if(SCANNER_STATE_t *state, AS_NODE *condition,
                            AS_NODE *then, AS_IF *elsif) {
  if (!then) {
    as_delete(condition);
    as_delete_if(elsif);
    parser_set_failure(state, NULL);
    return NULL;
  }
  AS_IF *newif = as_new_if(condition, then, elsif);
  if (!newif) {
    as_delete(condition);
    as_delete(then);
    as_delete_if(elsif);
    parser_set_failure(state, NULL);
  }
  return newif;
}

static AS_NODE *parser_new_if_node(SCANNER_STATE_t *state, AS_NODE *condition,
                                   AS_NODE *then, AS_IF *elsif) {
  if (!condition) {
    as_delete(then);
    as_delete_if(elsif);
    parser_set_failure(state, NULL);
    return NULL;
  }
  AS_IF *if_data = parser_new_if(state, condition, then, elsif);
  if (!if_data) return NULL;
  AS_NODE *node = as_new_node(N_IFSTMT, if_data, NULL);
  if (!node) {
    as_delete_if(if_data);
    parser_set_failure(state, NULL);
  }
  return node;
}

static AS_NODE *parser_new_unary_minus(AS_NODE *operand, SCANNER_STATE_t *state) {
  if (operand && operand->nodetype == N_VALUE) {
    AS_VALUE *value = (AS_VALUE *)operand->lhs;
    if (value) {
      if (value->valtype == V_INT && value->value.i > INT64_MIN) {
        value->value.i = -value->value.i;
        return operand;
      }
      if (value->valtype == V_FLOAT) {
        value->value.f_bits ^= UINT64_C(0x8000000000000000);
        return operand;
      }
    }
  }

  AS_NODE *zero = as_new_intnode(0);
  if (!zero) {
    as_delete(operand);
    parser_set_failure(state, NULL);
    return NULL;
  }
  return parser_new_node(state, N_SUB, zero, operand, true, true);
}

static AS_NODE *parser_new_libcall(SCANNER_STATE_t *state, char *library,
                                   char *name, AS_NODE *args) {
  AS_NODE *library_node = parser_new_value(state, V_LAYER, library);
  if (!library_node) {
    free(name);
    as_delete(args);
    return NULL;
  }
  AS_NODE *name_node = parser_new_value(state, V_LAYER, name);
  if (!name_node) {
    as_delete(library_node);
    as_delete(args);
    return NULL;
  }
  AS_NODE *tail = parser_new_node(state, N_ITEM, name_node, NULL, true, false);
  if (!tail) {
    as_delete(library_node);
    as_delete(args);
    return NULL;
  }
  AS_NODE *item = parser_new_node(state, N_ITEM, library_node, tail, true, true);
  if (!item) {
    as_delete(args);
    return NULL;
  }
  return parser_new_node(state, N_LIBCALL, item, args, true, false);
}

static AS_NODE *parser_new_code(SCANNER_STATE_t *state, AS_NODE *params,
                                char *body) {
  AS_NODE *body_node = parser_new_value(state, V_STR, body);
  if (!body_node) {
    as_delete(params);
    return NULL;
  }
  return parser_new_node(state, N_CODE, params, body_node, false, true);
}

static AS_NODE *parser_new_relative_item(SCANNER_STATE_t *state,
                                         AS_NODE *first, AS_NODE *rest) {
  AS_NODE *item = parser_new_node(state, N_ITEM, first, rest, true, false);
  if (!item) return NULL;
  return parser_new_node(state, N_RELITEM, item, NULL, true, false);
}

static AS_NODE *parser_new_list_elem(SCANNER_STATE_t *state, AS_NODE *value, AS_NODE *next) {
  return parser_new_node(state, N_LISTELEM, value, next, true, false);
}

void yyerror(YYLTYPE *locp, yyscan_t scanner, SCANNER_STATE_t *state, char const *s) {
  // yyerror() is called whenever there is a syntax error, so we need to
  // set the error number in the state appropriately.
  (void)scanner;
  if (locp && state->errnum == ERR_NOERROR) {
    state->line = locp->first_line;
    state->column = locp->first_column;
    state->span = locp->last_column >= locp->first_column ? locp->last_column - locp->first_column + 1 : 1;
  }
  if (state->errnum == ERR_NOERROR) {
    // This might have been set already so don't clobber it if it has
    state->errnum = ERR_COMP_SYNTAX;
  }
  if (state->errdetail == NULL) {
    if (state->source_name) {
      size_t n = strlen(state->source_name) + strlen(s) + 3;
      state->errdetail = malloc(n);
      if (state->errdetail) {
        snprintf(state->errdetail, n, "%s: %s", state->source_name, s);
      }
    } else {
      state->errdetail = parser_strdup(s);
    }
  }
}

int8_t parse_source_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, SCANNER_STATE_t *out_state) {
  // Compile the given string.
  // Returns 0 if successful or > 0 (error number) if not.
  // source holds the source input string
  // sourcelen holds the length of the input
  // Wrap all these bits of state up into a nice package
  // for ease of transport
  if (absyn) *absyn = NULL;
  if (errdetail) *errdetail = NULL;
  if (out_state) {
    memset(out_state, 0, sizeof *out_state);
    out_state->line = 1;
    out_state->column = 1;
    out_state->span = 1;
  }
  if (!input || !input->data || !absyn || !errdetail) {
    if (out_state) {
      out_state->errnum = ERR_COMP_SYNTAX;
      out_state->line = 1;
      out_state->column = 1;
      out_state->span = 1;
    }
    return ERR_COMP_SYNTAX;
  }
  if (input->len > (size_t)INT_MAX) {
    if (out_state) out_state->errnum = ERR_COMP_SYNTAX;
    return ERR_COMP_SYNTAX;
  }

  SCANNER_STATE_t *scanner_state = alloc_calloc(1, sizeof *scanner_state);
  if (!scanner_state) return ERR_COMP_SYNTAX;
  scanner_state->line = 1;
  scanner_state->column = 1;
  scanner_state->span = 1;
  scanner_state->source_name = input->source_name;

  volatile int parse_result = 1;
  int setup_failed = setjmp(scanner_state->scanner_fatal_jmp);
  if (setup_failed != 0) {
    parser_set_failure(scanner_state, "parser: scanner allocation failed");
  } else {
    scanner_state->scanner_fatal_jmp_active = true;
    int init_result = yylex_init_extra(scanner_state,
                                       (yyscan_t *)&scanner_state->scanner_handle);
    if (init_result != 0 || !scanner_state->scanner_handle) {
      parser_set_failure(scanner_state, "parser: scanner initialization failed");
    } else {
      YY_BUFFER_STATE in = yy_scan_bytes(input->data, (int)input->len,
                                         (yyscan_t)scanner_state->scanner_handle);
      if (!in) {
        parser_set_failure(scanner_state, "parser: scanner buffer creation failed");
      } else {
        parse_result = yyparse((yyscan_t)scanner_state->scanner_handle,
                               scanner_state);
        yy_delete_buffer(in, (yyscan_t)scanner_state->scanner_handle);
      }
    }
    scanner_state->scanner_fatal_jmp_active = false;
  }

  scanner_state->scanner_fatal_jmp_active = false;
  if (scanner_state->scanner_handle) {
    yylex_destroy((yyscan_t)scanner_state->scanner_handle);
    scanner_state->scanner_handle = NULL;
  }

  if (parse_result == 0 && scanner_state->errnum == ERR_NOERROR) {
    *absyn = scanner_state->absyn;
    scanner_state->absyn = NULL;
  } else {
    as_delete(scanner_state->absyn);
    scanner_state->absyn = NULL;
    if (scanner_state->errnum == ERR_NOERROR) {
      parser_set_failure(scanner_state, "parser: parsing failed");
    }
  }
  *errdetail = scanner_state->errdetail;
  scanner_state->errdetail = NULL;

  int8_t result = scanner_state->errnum;
  if (out_state) *out_state = *scanner_state;
  else free(scanner_state->offending_token);
  free(scanner_state);
  return result;
}

int8_t parse_source_compiler_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, CompilerDiagnostic *diag, SCANNER_STATE_t *out_state) {
  SCANNER_STATE_t state = {0};
  if (diag) compiler_diag_reset(diag);
  int8_t rc = parse_source_diag(input, absyn, errdetail, &state);
  if (rc != ERR_NOERROR && diag) {
    compiler_diag_set(diag, rc, DIAG_PHASE_PARSE, errdetail && *errdetail ? *errdetail : "");
    compiler_diag_set_source_name(diag, input && input->source_name ? input->source_name : "<memory>");
    compiler_diag_set_location(diag, state.line, state.column, state.span);
  }
  if (out_state) *out_state = state;
  else free(state.offending_token);
  return rc;
}

int8_t parse_source(const ParseInput *input, AS_NODE **absyn, char **errdetail) {
  SCANNER_STATE_t state = {0};
  int8_t rc = parse_source_diag(input, absyn, errdetail, &state);
  free(state.offending_token);
  return rc;
}

%}

%define api.value.type union /* Generate YYSTYPE from these types:  */
%token <char *> TINTEGER
%token <char *> TFLOAT
%token <char *> TSTRINGLIT
%token <char *> TLOCAL
%token <char *> TLAYER
%token <char *> TLIBNAME
%token <char *> TCODEBODY
%token <char *> TUNKNOWNCHAR
%token TTRUE TFALSE TNIL
%token TLISTSTART TITEMREF
%token TBREAK TCONTINUE
%token TSEMI TWHILE TDO TENDWHILE TIF TTHEN TELSE TELSIF TENDIF TRETURN
%token TASSIGN TINC TDEC TLAYERSEP TDEREFSTART TCODE TDEREFEND
%token TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA

%type <AS_NODE*> deref_content dereference first_layer subsequent_layers layer list list_elems itemref
%type <AS_NODE*> param_local param_list params item expr stmt stmtlist
%type <AS_NODE*> stmtsemi arg_list args item_assignment libcall
%type <AS_IF*> elsif_else_opt

// Precedence is ordered from low to high.  Keep OR below AND and keep
// equality/relational operators above both boolean operators.
// This intentionally changes mixed `and`/`or` grouping to conventional logic.
%left TOR
%left TAND
%left TEQUAL TNOTEQUAL TLT TGT TLTEQ TGTEQ
%left TPLUS TMINUS
%left TMULT TDIV TMOD
%precedence UMINUS TNOT

// Free lexer-allocated token strings when symbols are discarded by error
// recovery or parser teardown.
%destructor { free ($$); } TINTEGER TFLOAT TSTRINGLIT TLOCAL TLAYER TLIBNAME TCODEBODY TUNKNOWNCHAR
%destructor { as_delete($$); } <AS_NODE*>
%destructor { as_delete_if($$); } <AS_IF*>

%%

input:  stmtlist { state->absyn = $1; }
        ;

stmtlist: %empty {
            $$ = as_new_stmtlist_node();
            if (!$$) { parser_set_failure(state, NULL); YYERROR; }
          }
        | stmtlist stmtsemi {
            if (!as_stmtlist_append_checked($1, $2)) {
              $$ = NULL;
              as_delete($1);
              as_delete($2);
              parser_set_failure(state, "parser: out of memory growing statement list");
              YYERROR;
            }
            $$ = $1;
          }
        ;

stmtsemi: stmt TSEMI {
            $$ = $1;
            if (!$$) { parser_set_failure(state, NULL); YYERROR; }
          }
;

stmt:   TWHILE expr TDO stmtlist TENDWHILE {
          $$ = parser_new_node(state, N_WHILESTMT, $2, $4, true, true);
          if (!$$) YYERROR;
        }
        | TDO stmtlist TWHILE expr {
          $$ = parser_new_node(state, N_DOWHILESTMT, $4, $2, true, true);
          if (!$$) YYERROR;
        }
        | TIF expr TTHEN stmtlist elsif_else_opt TENDIF {
          $$ = parser_new_if_node(state, $2, $4, $5);
          if (!$$) YYERROR;
        }
        | TBREAK {
          $$ = parser_new_node(state, N_BREAK, NULL, NULL, false, false);
          if (!$$) YYERROR;
        }
        | TCONTINUE {
          $$ = parser_new_node(state, N_CONTINUE, NULL, NULL, false, false);
          if (!$$) YYERROR;
        }
        | TRETURN {
          $$ = parser_new_node(state, N_RETURN, NULL, NULL, false, false);
          if (!$$) YYERROR;
        }
        | TRETURN expr {
          $$ = parser_new_node(state, N_RETURN, $2, NULL, true, false);
          if (!$$) YYERROR;
        }
        | TLOCAL TASSIGN expr {
          $$ = parser_new_node(state, N_ASSLOCAL,
                               parser_new_value(state, V_LOCAL, $1), $3, true, true);
          if (!$$) YYERROR;
        }
        | item TASSIGN item_assignment {
          $$ = parser_new_node(state, N_ASSITEM, $1, $3, true, true);
          if (!$$) YYERROR;
        }
        | TLOCAL TINC {
          $$ = parser_new_node(state, N_INC,
                               parser_new_value(state, V_LOCAL, $1), NULL, true, false);
          if (!$$) YYERROR;
        }
        | TLOCAL TDEC {
          $$ = parser_new_node(state, N_DEC,
                               parser_new_value(state, V_LOCAL, $1), NULL, true, false);
          if (!$$) YYERROR;
        }
        | expr {
          $$ = parser_new_node(state, N_EXPRSTMT, $1, NULL, true, false);
          if (!$$) YYERROR;
        }
        ;

expr:     TLOCAL { $$ = parser_new_value(state, V_LOCAL, $1); if (!$$) YYERROR; }
        | TINTEGER { $$ = parser_new_integer(state, $1); if (!$$) YYERROR; }
        | TFLOAT { $$ = parser_new_value(state, V_FLOAT, $1); if (!$$) YYERROR; }
        | TSTRINGLIT { $$ = parser_new_value(state, V_STR, $1); if (!$$) YYERROR; }
        | TTRUE { $$ = parser_new_value(state, V_BOOLTRUE, NULL); if (!$$) YYERROR; }
        | TFALSE { $$ = parser_new_value(state, V_BOOLFALSE, NULL); if (!$$) YYERROR; }
        | TNIL { $$ = parser_new_value(state, V_NIL, NULL); if (!$$) YYERROR; }
        | list { $$ = $1; }
        | itemref { $$ = $1; }
        | item args { $$ = parser_new_node(state, N_CALL, $1, $2, true, false); if (!$$) YYERROR; }
        | expr TEQUAL expr { $$ = parser_new_node(state, N_EQUAL, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TNOTEQUAL expr { $$ = parser_new_node(state, N_NOTEQ, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TOR expr { $$ = parser_new_node(state, N_OR, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TAND expr { $$ = parser_new_node(state, N_AND, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TLT expr { $$ = parser_new_node(state, N_LT, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TLTEQ expr { $$ = parser_new_node(state, N_LTEQ, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TGT expr { $$ = parser_new_node(state, N_GT, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TGTEQ expr { $$ = parser_new_node(state, N_GTEQ, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TPLUS expr { $$ = parser_new_node(state, N_ADD, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TMINUS expr { $$ = parser_new_node(state, N_SUB, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TMULT expr { $$ = parser_new_node(state, N_MUL, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TDIV expr { $$ = parser_new_node(state, N_DIV, $1, $3, true, true); if (!$$) YYERROR; }
        | expr TMOD expr { $$ = parser_new_node(state, N_MOD, $1, $3, true, true); if (!$$) YYERROR; }
        | TLPAREN expr TRPAREN {
          $$ = $2;
          if (!$$) { parser_set_failure(state, NULL); YYERROR; }
        }
        | TNOT expr { $$ = parser_new_node(state, N_NOT, $2, NULL, true, false); if (!$$) YYERROR; }
        | TMINUS expr %prec UMINUS { $$ = parser_new_unary_minus($2, state); if (!$$) YYERROR; }
        | libcall { $$ = $1; }
        | TUNKNOWNCHAR { $$ = NULL;
                         state->errnum = ERR_COMP_UNKNOWNCHAR;
                         state->errdetail = $1;
                         state->line = @1.first_line;
                         state->column = @1.first_column;
                         state->span = @1.last_column >= @1.first_column ? @1.last_column - @1.first_column + 1 : 1;
                         free(state->offending_token);
                         state->offending_token = parser_strdup($1 ? $1 : "");
                         YYERROR;
                       }
        ;

libcall:  TLIBNAME TLAYERSEP TLAYER args {
          $$ = parser_new_libcall(state, $1, $3, $4);
          if (!$$) YYERROR;
        }
        ;

elsif_else_opt: %empty { $$ = NULL; }
        | TELSIF expr TTHEN stmtlist elsif_else_opt {
          if (!$2) {
            as_delete($4);
            as_delete_if($5);
            parser_set_failure(state, NULL);
            $$ = NULL;
            YYERROR;
          }
          $$ = parser_new_if(state, $2, $4, $5);
          if (!$$) YYERROR;
        }
        | TELSE stmtlist {
          $$ = parser_new_if(state, NULL, $2, NULL);
          if (!$$) YYERROR;
        }
        ;

params:   %empty { $$ = NULL; }
        | TLBRACE param_list TRBRACE { $$ = $2; }
        ;

param_list: param_local {
            $$ = parser_new_node(state, N_ARGLIST, $1, NULL, true, false);
            if (!$$) YYERROR;
          }
        | param_local TCOMMA param_list {
            $$ = parser_new_node(state, N_ARGLIST, $1, $3, true, true);
            if (!$$) YYERROR;
          }
        ;

param_local: TLOCAL { $$ = parser_new_value(state, V_LOCAL, $1); if (!$$) YYERROR; }
        ;

args:     %empty { $$ = NULL; }
        | TLBRACE arg_list TRBRACE { $$ = $2; }
        ;

arg_list: expr TCOMMA arg_list {
          $$ = parser_new_node(state, N_ARGLIST, $1, $3, true, true);
          if (!$$) YYERROR;
        }
        | expr {
          $$ = parser_new_node(state, N_ARGLIST, $1, NULL, true, false);
          if (!$$) YYERROR;
        }
        ;

item_assignment: expr { $$ = $1; }
        | TCODE params TCODEBODY {
          $$ = parser_new_code(state, $2, $3);
          if (!$$) YYERROR;
        }
        ;

list: TLISTSTART TDEREFEND {
        $$ = parser_new_node(state, N_LIST, NULL, NULL, false, false);
        if (!$$) YYERROR;
      }
      | TLISTSTART list_elems TDEREFEND {
        $$ = parser_new_node(state, N_LIST, $2, NULL, true, false);
        if (!$$) YYERROR;
      }
      ;

list_elems: expr {
             $$ = parser_new_list_elem(state, $1, NULL);
             if (!$$) YYERROR;
           }
           | expr TCOMMA list_elems {
             $$ = parser_new_list_elem(state, $1, $3);
             if (!$$) YYERROR;
           }
           ;

itemref: TITEMREF item {
          $$ = parser_new_node(state, N_ITEMREF, $2, NULL, true, false);
          if (!$$) YYERROR;
        }

item:     first_layer subsequent_layers {
          $$ = parser_new_node(state, N_ITEM, $1, $2, true, false);
          if (!$$) YYERROR;
        }
        | TLAYERSEP first_layer subsequent_layers {
          $$ = parser_new_relative_item(state, $2, $3);
          if (!$$) YYERROR;
        }
        ;

first_layer: TLAYER { $$ = parser_new_value(state, V_LAYER, $1); if (!$$) YYERROR; }
        | dereference { $$ = $1; }
        ;

subsequent_layers: %empty { $$ = NULL; }
        | TLAYERSEP layer subsequent_layers {
          $$ = parser_new_node(state, N_ITEM, $2, $3, true, false);
          if (!$$) YYERROR;
        }
        ;

layer:    TLAYER { $$ = parser_new_value(state, V_LAYER, $1); if (!$$) YYERROR; }
        | TINTEGER { $$ = parser_new_integer(state, $1); if (!$$) YYERROR; }
        | dereference { $$ = $1; }
;

dereference: TDEREFSTART deref_content TDEREFEND {
             $$ = parser_new_node(state, N_DEREF, $2, NULL, true, false);
             if (!$$) YYERROR;
           }
        ;

deref_content: item { $$ = $1; }
        | TLOCAL { $$ = parser_new_value(state, V_LOCAL, $1); if (!$$) YYERROR; }
        ;
%%
