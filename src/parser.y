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

  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    uint64_t maxsize;
  } OUTPUT_t;

  typedef struct {
    int8_t errnum;
  } SCANNER_STATE_t;

  int8_t parse_source(char *source, int sourcelen);
}

%{
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "parser.h"
#include "memory.h"
#include "absyn.h"
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

int8_t parse_source(char *source, int sourcelen) {
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
    return scanner_state.errnum;
  } else {
    // FIXME: The abstract syntax tree now exists:
    // It needs to be returned in the scanner state!
    return 0;
  }
}

%}

%union{
  char *string;
  int token;
}


%token <string> TINTEGER
%token <string> TSTRINGLIT
%token <string> TLOCAL
%token <string> TLAYER
%token <string> TLIBNAME
%token <string> TCODEBODY
%token <string> TUNKNOWNCHAR
%nonassoc TSEMI TWHILE TDO TENDWHILE TIF TTHEN TELSE TELSIF TENDIF TRETURN

%right TASSIGN
%left TEQUAL TNOTEQUAL TLESSTHAN TGREATERTHAN TLTEQ TGTEQ TAND TOR
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%left TLAYERSEP
%right TDEREFSTART TCODE
%left TDEREFEND
%nonassoc TEXISTS TDELETE TNTHNAME TROOTNAME
%right UMINUS TNOT
%nonassoc TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA

%%

input:  stmtlist
        ;

stmtlist: /* Nothing */
        | stmtlist stmtsemi
        ;

stmtsemi: stmt TSEMI
;

stmt:   TWHILE expr TDO stmtlist TENDWHILE
        | TIF expr
          TTHEN stmtlist
          elsif_else_opt TENDIF
        | TRETURN
        | TLOCAL TASSIGN expr
        | item TASSIGN item_assignment
        | TLOCAL TINC
        | TLOCAL TDEC
        | expr
        ;

expr:     TLOCAL
        |	TINTEGER
        |	TSTRINGLIT
        |	item args
        | expr TEQUAL expr
        | expr TNOTEQUAL expr
        | expr TOR expr
        | expr TAND expr
        | expr TLESSTHAN expr
        | expr TLTEQ expr
        | expr TGREATERTHAN expr
        | expr TGTEQ expr
        | expr TPLUS expr
	      |	expr TMINUS expr
	      |	expr TMULT expr
	      |	expr TDIV expr
        | TLPAREN expr TRPAREN
        | TNOT expr
        | TMINUS expr %prec UMINUS
        | funcop
        | libcall
        | TUNKNOWNCHAR
        ;

funcop:   TEXISTS TLBRACE item TRBRACE
        | TDELETE TLBRACE item TRBRACE
        | TNTHNAME TLBRACE item TCOMMA expr TRBRACE
        | TROOTNAME TLBRACE expr TRBRACE
        ;

libcall:  TLIBNAME TLAYERSEP TLAYER args
        ;

elsif_else_opt: /* empty */
        | TELSIF expr TTHEN stmtlist elsif_else_opt
        | TELSE stmtlist
        ;

params:   /* Nothing */
        | TLBRACE param_list TRBRACE
        ;

param_list: param_local
        | param_local TCOMMA param_list
        ;

param_local: TLOCAL
        ;

args:     /* Nothing */
        | TLBRACE arg_list TRBRACE
        ;

arg_list: expr TCOMMA arg_list
        | expr
        ;

item_assignment: expr
        | TCODE params TCODEBODY
        ;

item:     first_layer subsequent_layers
        ;

first_layer: TLAYER
        | first_layer_deref
        ;

subsequent_layers: /* Nothing */
        | TLAYERSEP layer subsequent_layers
        ;

layer:    TLAYER
        | TINTEGER
        | dereference
        ;

dereference: TDEREFSTART deref_content TDEREFEND
        ;

first_layer_deref: TDEREFSTART
        ;

deref_content: item
        | TLOCAL
        ;
%%

