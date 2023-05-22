/* This is a basic parser which takes an input source string and produces an
   output bytecode string which can be processed by the sin interpreter.
*/

%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}{OUTPUT_t *out}

%code requires {
  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    int maxsize;
  } OUTPUT_t;

  void parse_source(char *source, int sourcelen, OUTPUT_t *out);
}

%{
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "memory.h"
#include "log.h"

typedef void *yyscan_t;
int yylex (YYSTYPE *yylval_param, yyscan_t yyscanner);
int yylex_init(yyscan_t* scanner);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse();

void parse_source(char *source, int sourcelen, OUTPUT_t *out) {
  // source holds the source input string
  // sourcelen holds the length of the input
  // out is a pointer to a struct which holds the output buffer
  yyscan_t sc;

  logmsg("Parsing...\n");
  yylex_init(&sc);
  FILE *in = fmemopen(source, sourcelen, "r");
  yyset_in(in, sc);
  yyparse(sc, out);

  logmsg("Parse completed: %ld bytes.\n", out->nextbyte - out->bytecode);
  fclose(in);
  yylex_destroy(sc);
}

void grow_output_buffer(OUTPUT_t *out, unsigned int size) {
  // Check that the output buffer is big enough to hold another size bytes.
  // Grow it if necessary.
  int maybesize = out->nextbyte - out->bytecode + size;
  if (maybesize >= out->maxsize) {
    int oldsize = out->maxsize;
    out->maxsize = GROW_CAPACITY(out->maxsize + size);
    out->bytecode = GROW_ARRAY(unsigned char, out->bytecode, oldsize,
                                                              out->maxsize);
    out->nextbyte = out->bytecode + size;
  }
}

void emit_byte(unsigned char c, OUTPUT_t *out) {
  *out->nextbyte++ = c;
  grow_output_buffer(out, 1);
}

void emit_int64(uint64_t i, OUTPUT_t *out) {
  union { unsigned char c[8]; uint64_t i; } u;
  u.i = i;
  for (int x = 0; x < 8; x++) {
    emit_byte(u.c[x], out);
  }
}

void emit_int16(uint16_t i, OUTPUT_t *out) {
  union { unsigned char c[2]; uint16_t i; } u;
  u.i = i;
  for (int x = 0; x < 2; x++) {
    emit_byte(u.c[x], out);
  }
}

void emit_string(char *s, OUTPUT_t *out) {
  // First get the length of the string (minus the enclosing ")
  uint16_t l = strlen(s) - 2;
  // Then write out that 16-bit int
  emit_int16(l, out);
  // Then write out the string (again, minus the ")
  memcpy(out->nextbyte, s+1, l);
  out->nextbyte += l;
}
void yyerror(yyscan_t locp, OUTPUT_t *out, char const *s) {
  logerr("%s\n",s);
}

%}

%union{
  char *string;
  int token;
}

%start code

%token <string> TINTEGER
%token <string> TSTRINGLIT
%nonassoc TSEMI

%left TPLUS TMINUS
%left TMULT TDIV

%%

code:                             { emit_byte('h', out); }
        | stmts                   { emit_byte('h', out); }
        | stmts TSEMI             { emit_byte('h', out); }
;

stmts:    stmt                    { }
        | stmts TSEMI stmt        { }


stmt:     expr                    { }
;

expr:     expr TPLUS expr         { emit_byte('a', out); }
	      |	expr TMINUS expr        { emit_byte('s', out); }
	      |	expr TMULT expr         { emit_byte('m', out); }
	      |	expr TDIV expr          { emit_byte('d', out); }
        |	TINTEGER                { emit_byte('p', out);
                                    emit_int64(atoi(yylval.string), out); }
        |	TSTRINGLIT              { emit_byte('l', out);
                                    emit_string(yylval.string, out); }
;

%%

