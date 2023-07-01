/* This is a basic parser which takes an input source string and produces an
   output bytecode string which can be processed by the sin interpreter.
*/

%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}{SCANNER_STATE_t *state}

%code requires {
  #include <stdbool.h>
  #include <stdint.h>

  #define MAX_NESTED_LOOPS 32

  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    uint64_t maxsize;
  } OUTPUT_t;

  typedef struct {
    char *id[256];
    int count;
  } LOCAL_t;

  typedef struct {
    unsigned char *loop_start;
    unsigned char *loop_end;
    unsigned char *jump_to_start;
    unsigned char *jump_to_end;
  } LOOP_FIXUP_t;

  typedef struct {
    OUTPUT_t *out;
    LOCAL_t *local;
    int8_t loop_count;
    LOOP_FIXUP_t loop[MAX_NESTED_LOOPS];
  } SCANNER_STATE_t;

  bool parse_source(char *source, int sourcelen, OUTPUT_t *out);
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

void grow_output_buffer(OUTPUT_t *out, uint64_t size) {
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

bool parse_source(char *source, int sourcelen, OUTPUT_t *out) {
  // source holds the source input string
  // sourcelen holds the length of the input
  // out is a pointer to a struct which holds the output buffer
  yyscan_t sc;

  // Set up the locals table.  There are a maximum of 256 locals per
  // item, so just define an array on the stack.
  LOCAL_t local;
  local.count = 0;

  // Now wrap all these bits of state up into a nice package
  // for ease of transport
  SCANNER_STATE_t scanner_state;
  scanner_state.out = out;
  scanner_state.local = &local;
  scanner_state.loop_count = -1; // We start in no loop.

  logmsg("Parsing...\n");
  yylex_init(&sc);
  FILE *in = fmemopen(source, sourcelen, "r");
  yyset_in(in, sc);

  // Before we start parsing, make space in the output for the number
  // of locals - we will backfill the actual number later.
  emit_byte(0, out);

  bool failed = yyparse(sc, &scanner_state);

  // Clean up
  for (int l = 0; l < local.count; l++) {
    // This frees any locals which were put on the stack.
    free(local.id[l]);
  }
  fclose(in);
  yylex_destroy(sc);

  if (!failed) {
    // ...and now we know how many locals there are, update the first
    // byte of the bytecode.
    out->bytecode[0] = local.count;
    logmsg("Compilation completed: %ld bytes.\n",
                                          out->nextbyte - out->bytecode);
    return true;
  } else {
    logerr("Compilation failed.\n");
    return false;
  }
}

void emit_int64(uint64_t i, OUTPUT_t *out) {
  union { unsigned char c[8]; int64_t i; } u;
  u.i = i;
  for (int x = 0; x < 8; x++) {
    emit_byte(u.c[x], out);
  }
}

void emit_uint16(uint16_t i, OUTPUT_t *out) {
  union { unsigned char c[2]; uint16_t i; } u;
  u.i = i;
  for (int x = 0; x < 2; x++) {
    emit_byte(u.c[x], out);
  }
}

void emit_int16(uint16_t i, OUTPUT_t *out) {
  union { unsigned char c[2]; int16_t i; } u;
  u.i = i;
  for (int x = 0; x < 2; x++) {
    emit_byte(u.c[x], out);
  }
}

void emit_string(char *s, OUTPUT_t *out) {
  // First get the length of the string (minus the enclosing ")
  uint16_t l = strlen(s) - 2;
  // Then write out that uint16_t
  emit_uint16(l, out);
  // Then write out the string (again, minus the ")
  memcpy(out->nextbyte, s+1, l);
  out->nextbyte += l;
}
void yyerror(yyscan_t locp, SCANNER_STATE_t *state, char const *s) {
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
  return true;
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


void emit_local_increment(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // emit the "increment local" opcode, followed by the local to increment.
  emit_byte('f', out);
  emit_local_index(id, out, local);
}

void emit_local_decrement(char *id, OUTPUT_t *out, LOCAL_t *local) {
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

bool prepare_loop(SCANNER_STATE_t *state) {
  // We have encountered the start of a loop, so record it for
  // fixing up later.
  if (state->loop_count >= MAX_NESTED_LOOPS) {
    return false;
  }
  state->loop_count++; // We be looping.
  state->loop[state->loop_count].loop_start = state->out->nextbyte;
  return true;
}

bool finalise_loop(SCANNER_STATE_t *state) {
  // Calculate the offset from the jump-to-end to the actual end
  // Then fix up the jump instruction with the calculated value.
  int16_t offset = state->out->nextbyte
                          - state->loop[state->loop_count].jump_to_end;
  unsigned char *store_nextbyte = state->out->nextbyte; // Ugh
  state->out->nextbyte = state->loop[state->loop_count].jump_to_end;
  emit_int16(offset, state->out);
  state->out->nextbyte = store_nextbyte;
  state->loop_count--; // Loop be gone.
}

void emit_jump_to_end(SCANNER_STATE_t *state) {
  // Emit a conditional jump opcode with a dummy offset.  Record this in
  // the current loop record for fixing up later.
  emit_byte('k', state->out);
  state->loop[state->loop_count].jump_to_end = state->out->nextbyte;
  emit_int16(0, state->out);
}

void emit_jump_to_start(SCANNER_STATE_t *state) {
  // Emit an unconditional jump opcode and the offset to jump back
  // to the start of the loop.
  emit_byte('j', state->out);
  int16_t offset = state->loop[state->loop_count].loop_start
                                                  - state->out->nextbyte;
  emit_uint16(offset, state->out);
}

%}

%union{
  char *string;
  int token;
}


%token <string> TINTEGER
%token <string> TSTRINGLIT
%token <string> TLOCAL
%nonassoc TSEMI TCODE TWHILE TDO TENDWHILE

%right TASSIGN
%left TEQUAL TNOTEQUAL TLESSTHAN TGREATERTHAN TLTEQ TGTEQ
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%right UMINUS
%nonassoc TLPAREN TRPAREN

%%

program:  stmtlist { emit_byte('h', state->out); }
        ;

stmtlist: /* empty */
        | stmtlist stmtsemi
        ;

stmtsemi: stmt TSEMI
;

stmt:   TWHILE                  {
                if (!prepare_loop(state)) {
                  yyerror(scanner, state, "Maximum loop depth exceeded.\n");
                  YYERROR;
                }
                                  } expr {
                emit_jump_to_end(state);
                                  } TDO stmtlist TENDWHILE {
                emit_jump_to_start(state);
                finalise_loop(state);
                }
        | TLOCAL TASSIGN expr   { emit_local_assign($1, state->out, state->local); }
        | TLOCAL TINC           { emit_local_increment($1, state->out, state->local); }
        | TLOCAL TDEC           { emit_local_decrement($1, state->out, state->local); }
        | expr                  { }
        ;

expr:     TLOCAL                { emit_local($1, state->out, state->local); }
        |	TINTEGER              { emit_byte('p', state->out);
                                  emit_int64(atoi($1), state->out);
                                  free($1); }
        |	TSTRINGLIT            { emit_byte('l', state->out);
                                  emit_string($1, state->out);
                                  free($1); }
        | expr TEQUAL expr      { emit_byte('o', state->out); }
        | expr TNOTEQUAL expr   { emit_byte('q', state->out); }
        | expr TLESSTHAN expr   { emit_byte('r', state->out); }
        | expr TLTEQ expr       { emit_byte('u', state->out); }
        | expr TGREATERTHAN expr { emit_byte('t', state->out); }
        | expr TGTEQ expr       { emit_byte('v', state->out); }
        | expr TPLUS expr       { emit_byte('a', state->out); }
	      |	expr TMINUS expr      { emit_byte('s', state->out); }
	      |	expr TMULT expr       { emit_byte('m', state->out); }
	      |	expr TDIV expr        { emit_byte('d', state->out); }
        | TLPAREN expr TRPAREN  { }
        | TMINUS expr %prec UMINUS { emit_byte('n', state->out); }
;

%%

