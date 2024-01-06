/* This is a basic parser which takes an input source string and produces an
   output bytecode string which can be processed by the sin interpreter.
*/

%define api.pure full
%lex-param {void *scanner}
%parse-param {void *scanner}{SCANNER_STATE_t *state}

%code requires {
  #include <stdbool.h>
  #include <stdint.h>
  #include <malloc.h>

  /* This controls the depth of control structures
     (loops, IF statements, etc)
  */
  #define MAX_NESTED_CONTROLS 32

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
    unsigned char *addr;
    void *next;
  } IF_FIXUP_ADDR_t;

  typedef struct {
    unsigned char *next_else;
    IF_FIXUP_ADDR_t *list;
  } IF_FIXUP_t;

  typedef struct {
    OUTPUT_t *out;
    LOCAL_t *local;
    int8_t control_count;
    LOOP_FIXUP_t loop[MAX_NESTED_CONTROLS];
    IF_FIXUP_t if_stmt[MAX_NESTED_CONTROLS];
  } SCANNER_STATE_t;

  typedef struct yy_extra_type {
    int deref_depth;
  } yy_extra_type;

  #define YY_EXTRA_TYPE yy_extra_type
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
int yylex_init_extra(yy_extra_type my_extra, yyscan_t* sc);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse();

void yyerror(yyscan_t locp, SCANNER_STATE_t *state, char const *s) {
  logerr("%s\n",s);
}

void invalid_input(yyscan_t locp, SCANNER_STATE_t *state, char *msg) {
  char errmsg[100];
  snprintf(errmsg, 99, "Parse error: %s\n", msg);
  yyerror(locp, state, errmsg);
}

void emit_byte(unsigned char c, OUTPUT_t *out) {
    // Check if there is enough space in the buffer
    if (out->nextbyte - out->bytecode >= out->maxsize) {
        // Calculate the new buffer size
        int oldsize = out->maxsize;
        out->maxsize = GROW_CAPACITY(oldsize * 1.25);
        
        // Reallocate the buffer
        unsigned char *new_buffer = GROW_ARRAY(unsigned char, out->bytecode, oldsize, out->maxsize);
        
        // Update buffer pointers
        out->nextbyte = new_buffer + (out->nextbyte - out->bytecode);
        out->bytecode = new_buffer;
    }
    
    // Add the byte directly
    *out->nextbyte++ = c;
}

bool parse_source(char *source, int sourcelen, OUTPUT_t *out) {
  // source holds the source input string
  // sourcelen holds the length of the input
  // out is a pointer to a struct which holds the output buffer
  yyscan_t sc;
  yy_extra_type my_extra;
  my_extra.deref_depth = 0;

  // Set up the locals table.  There are a maximum of 256 locals per
  // item, so just define an array on the stack.
  LOCAL_t local;
  local.count = 0;

  // Now wrap all these bits of state up into a nice package
  // for ease of transport
  SCANNER_STATE_t scanner_state;
  scanner_state.out = out;
  scanner_state.local = &local;
  scanner_state.control_count = -1; // We start in no loop.

  logmsg("Parsing...\n");
  yylex_init_extra(my_extra, &sc);
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
  for (int j = 0; j < 8; j++) {
    emit_byte((unsigned char)(i & 0xFF), out);
    i >>= 8;
  }
}

void emit_int16(uint16_t i, OUTPUT_t *out) {
  // This is signedness-agnostic.  It just spits out two bytes.
  // It is up to the interpreter to know what to do with them.
  emit_byte((unsigned char)(i & 0xFF), out);
  emit_byte((unsigned char)((i >> 8) & 0xFF), out);
}

void emit_string(const char *s, OUTPUT_t *out) {
  uint16_t l = strlen(s);
  emit_int16(l, out);  // Write the length of the string (uint16_t)
  memcpy(out->nextbyte, s, l); // Copy the string directly (minus quotes)
  out->nextbyte += l;
}

void emit_layer(const char *s, OUTPUT_t *out) {
  // A bit like emit_string, but emits layers, which are not enclosed by
  // quotes, and have a maximum length of 255.
  uint8_t l = strlen(s);
  emit_byte(l, out);  // Write the length of the string (uint8_t)
  memcpy(out->nextbyte, s, l); // Copy the string directly (minus quotes)
  out->nextbyte += l;
}

bool emit_local_index(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // Find the identifier in the list and return its index.
  // Complain if not found.
  for (uint8_t l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) {
      emit_byte(l, out);
      return true; // Found, no need to continue searching.
    }
  }
  
  // No local found, this is a syntax error.
  logerr("Local variable used before assignment: %s\n", id);
  return false;
}

bool prepare_local_assign(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // Called before actually assigning a local variable.
  // This is necessary to enable the parser to clean up in the event
  // of a syntax error.

  // First check to see if the variable has been seen already
  for (uint8_t l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) {
      // Yes, so do nothing.
      return true;
    }
  }
  // This is the first time we have seen this local, so add it.
  // Check if the maximum number of locals has been reached.
  if (local->count >= 256) {
    logerr("Error: Maximum number of local variables (256) reached.\n");
    free(id);
    return false;
  }
  // Add the new local variable.
  local->id[local->count] = id;
  local->count++;
  return true;
}

bool emit_local_assign(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // prepare_local_assign() must be called before this!
  // Otherwise there will be no matching local variable.
  for (uint8_t l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) {
      // Local variable already exists, emit bytecode to assign it.
      emit_byte('c', out);
      emit_byte(l, out);
      return true;
    }
  }
  
  return false;
}

void emit_local_op(char *id, OUTPUT_t *out, LOCAL_t *local, char op) {
  // Emit the specified local operation ('f' for increment, 'g' for decrement),
  // followed by the local to increment or decrement.
  emit_byte(op, out);
  emit_local_index(id, out, local);
  free(id);
}

bool prepare_loop(SCANNER_STATE_t *state) {
  // We have encountered the start of a loop, so record it for
  // fixing up later.
  if (state->control_count >= MAX_NESTED_CONTROLS) {
    return false;
  }
  state->control_count++; // We be looping.
  state->loop[state->control_count].loop_start = state->out->nextbyte;
  return true;
}

void finalise_loop(SCANNER_STATE_t *state) {
  // Calculate the offset from the jump-to-end to the actual end
  // Then fix up the jump instruction with the calculated value.
  int16_t offset = state->out->nextbyte
                          - state->loop[state->control_count].jump_to_end;
  unsigned char *store_nextbyte = state->out->nextbyte; // Ugh
  state->out->nextbyte = state->loop[state->control_count].jump_to_end;
  emit_int16(offset, state->out);
  state->out->nextbyte = store_nextbyte;
  state->control_count--; // Loop be gone.
}

void emit_jump_to_end(SCANNER_STATE_t *state) {
  // Emit a conditional jump opcode with a dummy offset.  Record this in
  // the current loop record for fixing up later.
  emit_byte('k', state->out);
  state->loop[state->control_count].jump_to_end = state->out->nextbyte;
  emit_int16(0, state->out);
}

void emit_jump_to_start(SCANNER_STATE_t *state) {
  // Emit an unconditional jump opcode and the offset to jump back
  // to the start of the loop.
  emit_byte('j', state->out);
  int16_t offset = state->loop[state->control_count].loop_start
                                                  - state->out->nextbyte;
  emit_int16(offset, state->out);
}

bool prepare_if(SCANNER_STATE_t *state) {
  // Called before the first IF expression.
  // We have encountered the start of an if statement, so record it for
  // fixing up later.
  if (state->control_count >= MAX_NESTED_CONTROLS) {
    return false;
  }
  state->control_count++;
  state->if_stmt[state->control_count].next_else = NULL;
  state->if_stmt[state->control_count].list = NULL;
  return true;
}

void emit_jump_to_next_else(SCANNER_STATE_t *state) {
  // After every IF or ELSIF condition, there needs to be a jump-if-false
  // to the next condition (or to the end of the structure).  Insert a
  // placeholder for later updating.
  emit_byte('k', state->out);
  state->if_stmt[state->control_count].next_else = state->out->nextbyte;
  emit_int16(0, state->out);
}

void fixup_last_else_jump(SCANNER_STATE_t *state) {
  // When we encounter an ELSE/ELSIF, update the last jump-to-next-else
  // with the correct offset.
  int16_t offset = state->out->nextbyte
                          - state->if_stmt[state->control_count].next_else;
  unsigned char *store_nextbyte = state->out->nextbyte; // Ugh
  state->out->nextbyte = state->if_stmt[state->control_count].next_else;
  emit_int16(offset, state->out);
  state->out->nextbyte = store_nextbyte;
  state->if_stmt[state->control_count].next_else = NULL;
}

void emit_jump_to_endif(SCANNER_STATE_t *state) {
  // After every statement block in an IF statement, emit an unconditional
  // jump to the end of the statement with a placeholder for the offset.
  // Remember where the offset is, for later fixing-up
  emit_byte('j', state->out);
  IF_FIXUP_ADDR_t *fixup = malloc(sizeof(IF_FIXUP_ADDR_t));
  fixup->addr = state->out->nextbyte;
  fixup->next = NULL;
  if (state->if_stmt[state->control_count].list) {
    fixup->next = state->if_stmt[state->control_count].list;
  }
  state->if_stmt[state->control_count].list = fixup;
  emit_int16(0, state->out);
}

void finalise_if(SCANNER_STATE_t *state) {
  // Update all of the jump-to-end addresses with the correct offset.
  unsigned char *store_nextbyte = state->out->nextbyte;
  // There might be an ELSE jump still to fix, if this statement had no
  // ELSE clause.
  if (state->if_stmt[state->control_count].next_else) {
    fixup_last_else_jump(state);
  } else {
  }
  // Loop through state->if_stmt[state->control_count].list.
  // .list will be NULL if there are no more addresses to process.
  while (state->if_stmt[state->control_count].list) {
    // Calculate the offset to the end of the IF statement.
    int16_t offset = store_nextbyte -
                            state->if_stmt[state->control_count].list->addr;
    // Update the placeholder with the correct offset.
    state->out->nextbyte = state->if_stmt[state->control_count].list->addr;
    emit_int16(offset, state->out);
    // Free this address and loop to the next in the list.
    IF_FIXUP_ADDR_t *last = state->if_stmt[state->control_count].list;
    state->if_stmt[state->control_count].list =
                        state->if_stmt[state->control_count].list->next;
    free(last);
  }
  // All done.
  state->out->nextbyte = store_nextbyte;
  state->control_count--;
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
%token <string> TUNKNOWNCHAR
%nonassoc TSEMI TCODE TWHILE TDO TENDWHILE TIF TTHEN TELSE TELSIF TENDIF

%right TASSIGN
%left TEQUAL TNOTEQUAL TLESSTHAN TGREATERTHAN TLTEQ TGTEQ TAND TOR
%left TPLUS TMINUS
%left TMULT TDIV
%left TINC TDEC
%left TLAYERSEP
%right TDEREFSTART
%left TDEREFEND
%right UMINUS TNOT
%nonassoc TLPAREN TRPAREN

%%

input:  stmtlist { emit_byte('h', state->out); }
        ;

stmtlist: /* empty */
        | stmtlist stmtsemi
        ;

stmtsemi: stmt TSEMI
;

stmt:   TWHILE                  {
                if (!prepare_loop(state)) {
                  yyerror(scanner, state, "Maximum control structure depth exceeded.\n");
                  YYERROR;
                }
                                } expr {
                emit_jump_to_end(state);
                                } TDO stmtlist TENDWHILE {
                emit_jump_to_start(state);
                finalise_loop(state);
                }
        | TIF { prepare_if(state); } expr { emit_jump_to_next_else(state); }
          TTHEN stmtlist { emit_jump_to_endif(state); }
          elsif_else_opt TENDIF { finalise_if(state); }
        | TLOCAL TASSIGN { prepare_local_assign($1, state->out, state->local); } expr   { emit_local_assign($1, state->out, state->local); }
        | item { emit_byte('E', state->out); } TASSIGN expr {
                                  emit_byte('C', state->out);}
        | TLOCAL TINC           { emit_local_op($1, state->out, state->local, 'f'); }
        | TLOCAL TDEC           { emit_local_op($1, state->out, state->local, 'g'); }
        | expr                  { }
        ;

expr:     TLOCAL                { emit_local_op($1, state->out, state->local, 'e'); }
        |	TINTEGER              { emit_byte('p', state->out);
                                  emit_int64(atoi($1), state->out);
                                  free($1); }
        |	TSTRINGLIT            { emit_byte('l', state->out);
                                  emit_string($1, state->out);
                                  free($1); }
        |	item                  { emit_byte('E', state->out);
                                  emit_byte('F', state->out); }
        | expr TEQUAL expr      { emit_byte('o', state->out); }
        | expr TNOTEQUAL expr   { emit_byte('q', state->out); }
        | expr TOR expr         { emit_byte('z', state->out); }
        | expr TAND expr        { emit_byte('y', state->out); }
        | expr TLESSTHAN expr   { emit_byte('r', state->out); }
        | expr TLTEQ expr       { emit_byte('u', state->out); }
        | expr TGREATERTHAN expr { emit_byte('t', state->out); }
        | expr TGTEQ expr       { emit_byte('v', state->out); }
        | expr TPLUS expr       { emit_byte('a', state->out); }
	      |	expr TMINUS expr      { emit_byte('s', state->out); }
	      |	expr TMULT expr       { emit_byte('m', state->out); }
	      |	expr TDIV expr        { emit_byte('d', state->out); }
        | TLPAREN expr TRPAREN  { }
        | TNOT expr             { emit_byte('x', state->out); }
        | TMINUS expr %prec UMINUS { emit_byte('n', state->out); }
        | TUNKNOWNCHAR          {
                                  invalid_input(scanner, state, $1);
                                  free($1);
                                  YYERROR;
                                }
        ;

elsif_else_opt : /* empty */
        | TELSIF { fixup_last_else_jump(state); }
          expr { emit_jump_to_next_else(state); }
          TTHEN stmtlist { emit_jump_to_endif(state); } elsif_else_opt
        | TELSE { fixup_last_else_jump(state); } stmtlist
        ;

item:   first_layer
        | item TLAYERSEP layer
        ;

first_layer: { emit_byte('I', state->out); } layer
        ;

layer:  TLAYER { emit_byte('L', state->out);
                 emit_layer($1, state->out); free($1); }
        | dereference
        ;

dereference:  TDEREFSTART       { emit_byte('D', state->out); } deref_content TDEREFEND
        ;

deref_content: TLOCAL           { emit_local_op($1, state->out, state->local , 'V'); }
        | item                  { emit_byte('E', state->out); }
        ;

%%

