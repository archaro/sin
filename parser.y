/* This is a basic parser which takes an input source string and produces an
   output bytecode string which can be processed by the sin interpreter.
*/

%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}{OUTPUT_t *out}{LOCAL_t *local}

%code requires {
  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    int maxsize;
  } OUTPUT_t;

  typedef struct {
    char *id[256];
    int count;
  } LOCAL_t;

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

void parse_source(char *source, int sourcelen, OUTPUT_t *out) {
  // source holds the source input string
  // sourcelen holds the length of the input
  // out is a pointer to a struct which holds the output buffer
  yyscan_t sc;

  // Set up the locals table.  There are a maximum of 256 locals per
  // item, so just define an array on the stack.
  LOCAL_t local;

  local.id;
  local.count = 0;

  logmsg("Parsing...\n");
  yylex_init(&sc);
  FILE *in = fmemopen(source, sourcelen, "r");
  yyset_in(in, sc);

  // Before we start parsing, make space in the output for the number
  // of locals - we will backfill the actual number later.
  emit_byte(0, out);

  yyparse(sc, out, &local);

  // ...and now we know how many locals there are, update the first
  // byte of the bytecode.
  out->bytecode[0] = local.count;

  logmsg("Parse completed: %ld bytes.\n", out->nextbyte - out->bytecode);
  for (int l = 0; l < local.count; l++) {
    free(local.id[l]);
  }
  fclose(in);
  yylex_destroy(sc);
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
void yyerror(yyscan_t locp, OUTPUT_t *out, LOCAL_t *local, char const *s) {
  logerr("%s\n",s);
}

bool emit_local_index(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // Find the identifier in the list, and return its index.
  // Complain if not found.
  uint8_t l;
 
  // First check to see if the local exists
  for (l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) {
      break;
    }
  }
  if (l == local->count) {
    // No local found, so this is a syntax error.
    logerr("Local variable used before assignment.\n");
    free(id);
    return false;
  }
  free(id);
  emit_byte(l, out);
}

bool emit_local_assign(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // This function is called when a local assignment statement is
  // parsed.  It adds the the local to the list of locals and gives
  // it a unique index.  Up to 255 locals are permitted per item.

  uint8_t l;

  // First check to see if the local exists
  for (l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) break;
  }
  // If there is space, add it
  if (local->count >= 256) {
    logerr("Too many local variables.\n");
    free(id);
    return false;
  }
  // If we get here, there is space for the local. 
  // We don't free the id here because it is needed for lookups.
  // It is freed in parse_source().
  if (local->count == l) {
    local->id[local->count] = id;
    local->count++;
  }
    
  // We now have a new local.  Emit the bytecode to store the top
  // of the stack in the indexed stack location.
  emit_byte('c', out);
  emit_byte(l, out);
  return true;
}


bool emit_local_increment(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // emit the "increment local" opcode, followed by the local to increment.
  emit_byte('f', out);
  emit_local_index(id, out, local);
}

bool emit_local_decrement(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // emit the "decrement local" opcode, followed by the local to decrement.
  emit_byte('g', out);
  emit_local_index(id, out, local);
}

bool emit_local(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // This function emits the bytecode necessary to push the value of a local
  // variable onto the stack.
  emit_byte('e', out);
  emit_local_index(id, out, local);
  return true;
}

%}

%union{
  char *string;
  int token;
}

%start code

%token <string> TINTEGER
%token <string> TSTRINGLIT
%token <string> TLOCAL
%nonassoc TSEMI

%right TASSIGN
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%left UMINUS
%nonassoc TLPAREN TRPAREN

%%

code:                             { emit_byte('h', out); }
        | stmts                   { emit_byte('h', out); }
        | stmts TSEMI             { emit_byte('h', out); }
;

stmts:    stmt                    { }
        | stmts TSEMI stmt        { }


stmt:     expr                    { }
        | TLOCAL TASSIGN expr     { emit_local_assign($1, out, local); }
        | TLOCAL TINC             { emit_local_increment($1, out, local); }
        | TLOCAL TDEC             { emit_local_decrement($1, out, local); }
;

expr:     TLOCAL                  { emit_local(yylval.string, out, local); }
        |	TINTEGER                { emit_byte('p', out);
                                    emit_int64(atoi(yylval.string), out);
                                    free(yylval.string); }
        |	TSTRINGLIT              { emit_byte('l', out);
                                    emit_string(yylval.string, out);
                                    free(yylval.string); }
        | expr TPLUS expr         { emit_byte('a', out); }
	      |	expr TMINUS expr        { emit_byte('s', out); }
	      |	expr TMULT expr         { emit_byte('m', out); }
	      |	expr TDIV expr          { emit_byte('d', out); }
        | TLPAREN expr TRPAREN    { }
        | TMINUS expr %prec UMINUS { emit_byte('n', out); }
;

%%

