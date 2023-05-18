/* This is a basic lexer which takes an input source file and produces an
    output file which can be processed by the sin interpreter.
 */

%{
#include <stdint.h>

FILE *in, *out;

unsigned char *bytecode, *nextbyte;
int maxsize = 1024;

void init_output() {
  // Set up the output buffer
  bytecode = (char *)malloc(maxsize);
  nextbyte = bytecode;
}

void out_byte(unsigned char c) {
  *nextbyte++ = c;
  int size = nextbyte - bytecode;
  if (size >= maxsize) {
    maxsize *= 2;
    bytecode = realloc(bytecode, maxsize);
    nextbyte = bytecode + size;
  }
}

void emitint(int i) {
  union { unsigned char c[8]; uint64_t i; } u;
  u.i = i;
  for (int x = 0; x < 8; x++) {
    out_byte(u.c[x]);
  }
}

%}

%%

"e"	out_byte(yytext[0]);
"h"	out_byte(yytext[0]);
"p"	out_byte(yytext[0]);

[0-9]+	emitint(atoi(yytext));

%%

int yywrap() { return 1; }

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Syntax: maketest <input file> <output file>\n");
    exit(1);
  }

  in = fopen(argv[1], "r");
  if (!in) {
    printf("Unable to open input file.");
    exit(1);
  }
  out = fopen(argv[2], "w");
  if (!in) {
    printf("Unable to open output file.");
    exit(1);
  }

  init_output();

  yyin = in;
  yylex();
  fclose(in);

  fwrite(bytecode, nextbyte - bytecode, 1, out);
  fclose(out);
  return 0;
}

