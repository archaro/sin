/* This is a basic parser which takes an input source string and produces an
   abstract syntax tree, which will be used to generate the sinistra bytecode.

   Licensed under the MIT License - see LICENSE file for details.
*/


%define api.pure full
%locations
%lex-param {void *scanner}
%parse-param {void *scanner}{SCANNER_STATE_t *state}

%code requires {
  #include <stdbool.h>
  #include <stdint.h>

  #include "absyn.h"
  #include "parse_input.h"

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
  } SCANNER_STATE_t;

  int8_t parse_source(const ParseInput *input, AS_NODE **absyn, char **errdetail);
  int8_t parse_source_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, SCANNER_STATE_t *out_state);
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
int yylex_init(yyscan_t* scanner);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
void yyset_extra(SCANNER_STATE_t *user_defined, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse();
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len, yyscan_t yyscanner);
void yy_delete_buffer(YY_BUFFER_STATE b, yyscan_t yyscanner);


static AS_NODE *as_new_unary_minus_node(AS_NODE *operand) {
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

  return as_new_node(N_SUB, as_new_intnode(0), operand);
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
      state->errdetail = strdup(s);
    }
  }
}

int8_t parse_source_diag(const ParseInput *input, AS_NODE **absyn, char **errdetail, SCANNER_STATE_t *out_state) {
  // Compile the given string.
  // Returns 0 if successful or > 0 (error number) if not.
  // source holds the source input string
  // sourcelen holds the length of the input
  yyscan_t sc;
  yylex_init(&sc);
  // Wrap all these bits of state up into a nice package
  // for ease of transport
  SCANNER_STATE_t scanner_state;
  scanner_state.errnum = ERR_NOERROR;
  scanner_state.errdetail = NULL;
  scanner_state.absyn = NULL;
  scanner_state.line = 1;
  scanner_state.column = 1;
  scanner_state.span = 1;
  scanner_state.offending_token = NULL;
  if (!input || !input->data || !absyn || !errdetail) {
    return ERR_COMP_SYNTAX;
  }
  scanner_state.source_name = input->source_name;
  yyset_extra(&scanner_state, sc);
  YY_BUFFER_STATE in = yy_scan_bytes(input->data, (int)input->len, sc);

  bool failed = yyparse(sc, &scanner_state);

  // Clean up
  yy_delete_buffer(in, sc);
  yylex_destroy(sc);

  if (failed) {
    if (scanner_state.absyn != NULL) {
      as_delete(scanner_state.absyn);
      scanner_state.absyn = NULL;
    }
    *errdetail = scanner_state.errdetail;
    scanner_state.errdetail = NULL;
    if (out_state) *out_state = scanner_state;
    else free(scanner_state.offending_token);
    return scanner_state.errnum;
  } else {
    // scanner_state.absyn now points to the root of the abstract syntax tree
    *absyn = scanner_state.absyn;
    *errdetail = NULL;
    if (out_state) *out_state = scanner_state;
    else free(scanner_state.offending_token);
    return 0;
  }
}

int8_t parse_source(const ParseInput *input, AS_NODE **absyn, char **errdetail) {
  SCANNER_STATE_t state;
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
%token TTRUE TFALSE
%nonassoc TSEMI TWHILE TDO TENDWHILE TIF TTHEN TELSE TELSIF TENDIF TRETURN

%type <AS_NODE*> deref_content dereference first_layer subsequent_layers layer
%type <AS_NODE*> param_local param_list params funcop item expr stmt stmtlist
%type <AS_NODE*> stmtsemi arg_list args item_assignment libcall
%type <AS_IF*> elsif_else_opt

%right TASSIGN
// Precedence is ordered from low to high.  Keep OR below AND and keep
// equality/relational operators above both boolean operators.
// This intentionally changes mixed `and`/`or` grouping to conventional logic.
%left TOR
%left TAND
%left TEQUAL TNOTEQUAL TLT TGT TLTEQ TGTEQ
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%left TLAYERSEP
%right TDEREFSTART TCODE
%left TDEREFEND
%nonassoc TEXISTS TDELETE TNTHNAME TROOTNAME
%right UMINUS TNOT
%nonassoc TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA

// Free lexer-allocated token strings when symbols are discarded by error
// recovery or parser teardown.
%destructor { free ($$); } TINTEGER TFLOAT TSTRINGLIT TLOCAL TLAYER TLIBNAME TCODEBODY TUNKNOWNCHAR
%destructor { as_delete($$); } <AS_NODE*>
%destructor { as_delete_if($$); } <AS_IF*>

%%

input:  stmtlist { state->absyn = $1; }
        ;

stmtlist: /* Nothing */ { $$ = as_new_stmtlist_node(); }
        | stmtlist stmtsemi {
            if (!as_stmtlist_append_checked($1, $2)) {
              $$ = NULL;
              state->errnum = ERR_COMP_SYNTAX;
              state->errdetail = strdup("parser: out of memory growing statement list");
              YYERROR;
            }
            $$ = $1;
          }
        ;

stmtsemi: stmt TSEMI { $$ = $1; }
;

stmt:   TWHILE expr TDO stmtlist TENDWHILE { $$ = as_new_node(N_WHILESTMT, $2, $4); }
        | TIF expr TTHEN stmtlist elsif_else_opt TENDIF { $$ = as_new_node(N_IFSTMT, as_new_if($2, $4, $5), NULL); }
        | TRETURN { $$ = as_new_node(N_RETURN, NULL, NULL); }
        | TLOCAL TASSIGN expr { $$ = as_new_node(N_ASSLOCAL, as_new_valnode(V_LOCAL, $1), $3); }
        | item TASSIGN item_assignment { $$ = as_new_node(N_ASSITEM, $1, $3); }
        | TLOCAL TINC { $$ = as_new_node(N_INC, as_new_valnode(V_LOCAL, $1), NULL); }
        | TLOCAL TDEC { $$ = as_new_node(N_DEC, as_new_valnode(V_LOCAL, $1), NULL); }
        | expr { $$ = as_new_node(N_EXPRSTMT, $1, NULL); }
        ;

expr:     TLOCAL { $$ = as_new_valnode(V_LOCAL, $1); }
        |	TINTEGER { $$ = as_new_valnode(V_INT, $1); }
        | TFLOAT { $$ = as_new_valnode(V_FLOAT, $1); }
        |	TSTRINGLIT { $$ = as_new_valnode(V_STR, $1); }
        | TTRUE { $$ = as_new_valnode(V_BOOLTRUE, NULL); }
        | TFALSE { $$ = as_new_valnode(V_BOOLFALSE, NULL); }
        |	item args { $$ = as_new_node(N_CALL, $1, $2); }
        | expr TEQUAL expr { $$ = as_new_node(N_EQUAL, $1, $3); }
        | expr TNOTEQUAL expr { $$ = as_new_node(N_NOTEQ, $1, $3); }
        | expr TOR expr { $$ = as_new_node(N_OR, $1, $3); }
        | expr TAND expr { $$ = as_new_node(N_AND, $1, $3); }
        | expr TLT expr { $$ = as_new_node(N_LT, $1, $3); }
        | expr TLTEQ expr { $$ = as_new_node(N_LTEQ, $1, $3); }
        | expr TGT expr { $$ = as_new_node(N_GT, $1, $3); }
        | expr TGTEQ expr { $$ = as_new_node(N_GTEQ, $1, $3); }
        | expr TPLUS expr { $$ = as_new_node(N_ADD, $1, $3); }
	      |	expr TMINUS expr { $$ = as_new_node(N_SUB, $1, $3); }
	      |	expr TMULT expr { $$ = as_new_node(N_MUL, $1, $3); }
	      |	expr TDIV expr { $$ = as_new_node(N_DIV, $1, $3); }
        | TLPAREN expr TRPAREN { $$ = $2; }
        | TNOT expr { $$ = as_new_node(N_NOT, $2, NULL); }
        | TMINUS expr %prec UMINUS { $$ = as_new_unary_minus_node($2); }
        | funcop { $$ = $1; }
        | libcall { $$ = $1; }
        | TUNKNOWNCHAR { $$ = NULL;
                         state->errnum = ERR_COMP_UNKNOWNCHAR;
                         state->errdetail = $1;
                         state->line = @1.first_line;
                         state->column = @1.first_column;
                         state->span = @1.last_column >= @1.first_column ? @1.last_column - @1.first_column + 1 : 1;
                         free(state->offending_token);
                         state->offending_token = strdup($1 ? $1 : "");
                         YYERROR;
                       }
        ;

funcop:   TEXISTS TLBRACE item TRBRACE { $$ = as_new_node(N_EXISTS, $3, NULL); }
        | TDELETE TLBRACE item TRBRACE { $$ = as_new_node(N_DELETE, $3, NULL); }
        | TNTHNAME TLBRACE item TCOMMA expr TRBRACE { $$ = as_new_node(N_NTHNAME, $3, $5); }
        | TROOTNAME TLBRACE expr TRBRACE { $$ = as_new_node(N_ROOTNAME, $3, NULL); }
        ;

libcall:  TLIBNAME TLAYERSEP TLAYER args { $$ = as_new_node(N_LIBCALL, as_new_node(N_ITEM, as_new_valnode(V_LAYER, $1), as_new_node(N_ITEM, as_new_valnode(V_LAYER, $3), NULL)), $4); };
        ;

elsif_else_opt: /* empty */ { $$ = NULL; }
        | TELSIF expr TTHEN stmtlist elsif_else_opt { $$ = as_new_if($2, $4, $5); }
        | TELSE stmtlist { $$ = as_new_if(NULL, $2, NULL); }
        ;

params:   /* Nothing */ { $$ = NULL; }
        | TLBRACE param_list TRBRACE { $$ = $2; }
        ;

param_list: param_local { $$ = as_new_node(N_ARGLIST, $1, NULL); }
        | param_local TCOMMA param_list { $$ = as_new_node(N_ARGLIST, $1, $3); }
        ;

param_local: TLOCAL { $$ = as_new_valnode(V_LOCAL, $1); }
        ;

args:     /* Nothing */ { $$ = NULL; }
        | TLBRACE arg_list TRBRACE { $$ = $2; }
        ;

arg_list: expr TCOMMA arg_list { $$ = as_new_node(N_ARGLIST, $1, $3); }
        | expr { $$ = as_new_node(N_ARGLIST, $1, NULL); }
        ;

item_assignment: expr { $$ = $1; }
        | TCODE params TCODEBODY { $$ = as_new_node(N_CODE, $2, as_new_valnode(V_STR, $3)); }
        ;

item:     first_layer subsequent_layers { $$ = as_new_node(N_ITEM, $1, $2); }
        | TLAYERSEP first_layer subsequent_layers { $$ = as_new_node(N_RELITEM, as_new_node(N_ITEM, $2, $3), NULL); }
        ;

first_layer: TLAYER { $$ = as_new_valnode(V_LAYER, $1); }
        | dereference { $$ = $1; }
        ;

subsequent_layers: /* Nothing */ { $$ = NULL; }
        | TLAYERSEP layer subsequent_layers { $$ = as_new_node(N_ITEM, $2, $3); }
        ;

layer:    TLAYER { $$ = as_new_valnode(V_LAYER, $1); }
        | TINTEGER { $$ = as_new_valnode(V_INT, $1); }
        | dereference { $$ = $1; }
        ;

dereference: TDEREFSTART deref_content TDEREFEND { $$ = as_new_node(N_DEREF, $2, NULL); }
        ;

deref_content: item { $$ = $1; }
        | TLOCAL { $$ = as_new_valnode(V_LOCAL, $1); }
        ;
%%
