/* This is a basic lexer which takes an input source file and produces an
   output file which can be processed by the sin interpreter.
*/

%{
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

#include "memory.h"

int yylex(void);
void yyerror(char const *);

extern FILE *yyin;
FILE *out;
unsigned char *bytecode, *nextbyte;
int maxsize = 1024;

void init_output() {
  // Set up the output buffer
  bytecode = GROW_ARRAY(unsigned char, bytecode, 0, maxsize);
  nextbyte = bytecode;
}

void emit_byte(unsigned char c) {
  *nextbyte++ = c;
  int size = nextbyte - bytecode;
  if (size >= maxsize) {
    int oldsize = maxsize;
    maxsize = GROW_CAPACITY(maxsize);
    bytecode = GROW_ARRAY(unsigned char, bytecode, oldsize, maxsize);
    nextbyte = bytecode + size;
  }
}

void emit_int(int i) {
  union { unsigned char c[8]; uint64_t i; } u;
  u.i = i;
  for (int x = 0; x < 8; x++) {
    emit_byte(u.c[x]);
  }
}

%}

%union{
  char *string;
  int token;
}

%start code

%token <string> TINTEGER
%nonassoc TSEMI

%left TPLUS TMINUS

%%

code:                               { emit_byte('h'); }
        | stmts                     { emit_byte('h'); }
        | stmts TSEMI               { emit_byte('h'); }
;

stmts:    stmt                      { }
        | stmts TSEMI stmt          { }
;

stmt:     expr                      { }
;

expr:     expr TPLUS expr           { emit_byte('a'); }
	      |	expr TMINUS expr          { emit_byte('s'); }
        |	TINTEGER                  { emit_byte('p'); emit_int(atoi(yylval.string)); }
;

%%
int yyparse();
int main(int argc, char **argv) {

printf("Starting...\n");
  if (argc != 3) {
    printf("Syntax: maketest <input file> <output file>\n");
    exit(1);
  }
  yyin = fopen(argv[1], "r");
  if (!yyin) {
    printf("Unable to open input file.");
    exit(1);
  }
  out = fopen(argv[2], "w");
  if (!out) {
    printf("Unable to open output file.");
    exit(1);
  }

  init_output();
  yyparse();
  fclose(yyin);

  printf("\nParse completed.\n");
  fwrite(bytecode, nextbyte - bytecode, 1, out);
  fclose(out);
  FREE_ARRAY(unsigned char, bytecode, maxsize);
}


/*int main(void) {return yyparse();}*/
void yyerror(char const *s) {fprintf(stderr,"%s\n",s);}

