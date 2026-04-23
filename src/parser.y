/* This is a basic parser which takes an input source string and produces an
   abstract syntax tree, which will be used to generate the sinistra bytecode.

   Licensed under the MIT License - see LICENSE file for details.
*/


%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}{SCANNER_STATE_t *state}

%code requires {
  #include <stdbool.h>
  #include <stdint.h>

  #include "absyn.h"

  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    uint64_t maxsize;
  } OUTPUT_t;

  typedef struct {
    int8_t errnum;
    AS_NODE *absyn;
  } SCANNER_STATE_t;

  int8_t parse_source(char *source, int sourcelen, AS_NODE **absyn);
}

%{
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "parser.h"
#include "memory.h"
#include "libcall.h"

typedef void *yyscan_t;
int yylex (YYSTYPE *yylval_param, yyscan_t yyscanner);
int yylex_init(yyscan_t* scanner);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse();

void yyerror(yyscan_t locp, SCANNER_STATE_t *state, char const *s) {
  // yyerror() is called whenever there is a syntax error, so we need to
  // set the error number in the state appropriately.
  state->errnum = ERR_COMP_SYNTAX;
}

int8_t parse_source(char *source, int sourcelen, AS_NODE **absyn) {
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
  FILE *in = fmemopen(source, sourcelen, "r");
  yyset_in(in, sc);

  bool failed = yyparse(sc, &scanner_state);

  // Clean up
  fclose(in);
  yylex_destroy(sc);

  if (failed) {
    scanner_state.absyn = NULL;
    return scanner_state.errnum;
  } else {
    // scanner_state.absyn now points to the root of the abstract syntax tree
    *absyn = scanner_state.absyn;
    return 0;
  }
}

%}

%define api.value.type union /* Generate YYSTYPE from these types:  */
%token <char *> TINTEGER
%token <char *> TSTRINGLIT
%token <char *> TLOCAL
%token <char *> TLAYER
%token <char *> TLIBNAME
%token <char *> TCODEBODY
%token <char *> TUNKNOWNCHAR
%nonassoc TSEMI TWHILE TDO TENDWHILE TIF TTHEN TELSE TELSIF TENDIF TRETURN

%type <AS_NODE*> deref_content dereference first_layer subsequent_layers layer
%type <AS_NODE*> param_local param_list params funcop item expr stmt stmtlist
%type <AS_NODE*> stmtsemi arg_list args item_assignment libcall
%type <AS_IF*> elsif_else_opt

%right TASSIGN
%left TEQUAL TNOTEQUAL TLT TGT TLTEQ TGTEQ TAND TOR
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%left TLAYERSEP
%right TDEREFSTART TCODE
%left TDEREFEND
%nonassoc TEXISTS TDELETE TNTHNAME TROOTNAME
%right UMINUS TNOT
%nonassoc TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA

%destructor { free ($$); } <*>

%%

input:  stmtlist { state->absyn = $1; }
        ;

stmtlist: /* Nothing */ { $$ = NULL; }
        | stmtlist stmtsemi { $$ = as_new_node(N_STMT, $2, $1); }
        ;

stmtsemi: stmt TSEMI { $$ = as_new_node(N_STMT, $1, NULL); }
;

stmt:   TWHILE expr TDO stmtlist TENDWHILE { $$ = as_new_node(N_WHILESTMT, $4, $2); }
        | TIF expr TTHEN stmtlist elsif_else_opt TENDIF { $$ = as_new_node(N_IFSTMT, as_new_if($2, $4, $5), NULL); }
        | TRETURN { $$ = as_new_node(N_RETURN, NULL, NULL); }
        | TLOCAL TASSIGN expr { $$ = as_new_node(N_ASSLOCAL, $1, $3); }
        | item TASSIGN item_assignment { $$ = as_new_node(N_ASSITEM, $1, $3); }
        | TLOCAL TINC { $$ = as_new_node(N_INC, $1, NULL); }
        | TLOCAL TDEC { $$ = as_new_node(N_DEC, $1, NULL); }
        | expr { $$ = as_new_node(N_EXPRSTMT, $1, NULL); }
        ;

expr:     TLOCAL { $$ = as_new_valnode(V_LOCAL, $1); }
        |	TINTEGER { $$ = as_new_valnode(V_INT, $1); }
        |	TSTRINGLIT { $$ = as_new_valnode(V_STR, $1); }
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
        | TMINUS expr %prec UMINUS { $$ = as_new_node(N_SUB, as_new_valnode(V_INT, "0"), $2); }
        | funcop { $$ = $1; }
        | libcall { $$ = $1; }
        | TUNKNOWNCHAR { $$ = NULL;
                         state->errnum = ERR_COMP_UNKNOWNCHAR;
                         free($1);
                         YYERROR;
                       }
        ;

funcop:   TEXISTS TLBRACE item TRBRACE { $$ = as_new_node(N_EXISTS, $3, NULL); }
        | TDELETE TLBRACE item TRBRACE { $$ = as_new_node(N_DELETE, $3, NULL); }
        | TNTHNAME TLBRACE item TCOMMA expr TRBRACE { $$ = as_new_node(N_NTHNAME, $3, $5); }
        | TROOTNAME TLBRACE expr TRBRACE { $$ = as_new_node(N_ROOTNAME, $3, NULL); }
        ;

libcall:  TLIBNAME TLAYERSEP TLAYER args { $$ = as_new_node(N_LIBCALL, as_new_node(N_ITEM, $1, as_new_node(N_ITEM, $3, NULL)), $4); };
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
        | TCODE params TCODEBODY { $$ = as_new_node(N_CODE, $2, $3); }
        ;

item:     first_layer subsequent_layers { $$ = as_new_node(N_ITEM, $1, $2); }
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

