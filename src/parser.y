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
  #define MAX_LOCAL_VARS 256

  typedef struct {
    unsigned char *bytecode;
    unsigned char *nextbyte;
    uint64_t maxsize;
  } OUTPUT_t;

  typedef struct {
    char *id[MAX_LOCAL_VARS];
    int count;
    int param_count;
    int8_t errnum;
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
    int8_t item_count;
    int arg_count[MAX_NESTED_CONTROLS];
    OUTPUT_t *item_buf;
    OUTPUT_t *item_out[MAX_NESTED_CONTROLS];
    LOOP_FIXUP_t loop[MAX_NESTED_CONTROLS];
    IF_FIXUP_t if_stmt[MAX_NESTED_CONTROLS];
  } SCANNER_STATE_t;

  typedef struct yy_extra_type {
    int deref_depth;
  } yy_extra_type;

  #define YY_EXTRA_TYPE yy_extra_type
  bool parse_source(char *source, int sourcelen, OUTPUT_t *out,
                                                           LOCAL_t *local);
}

%{
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "parser.h"
#include "memory.h"
#include "libcalls.h"

typedef void *yyscan_t;
int yylex (YYSTYPE *yylval_param, yyscan_t yyscanner);
int yylex_init(yyscan_t* scanner);
int yylex_init_extra(yy_extra_type my_extra, yyscan_t* sc);
void yyset_in(FILE *_in_str, yyscan_t yyscanner);
int yylex_destroy(yyscan_t yyscanner);
int yyparse();

void yyerror(yyscan_t locp, SCANNER_STATE_t *state, char const *s) {
  // We don't actually do anything in yyerror, we just need to define it.
  // yyerror() is called whenever there is a syntax error, so we need to
  // set the error number in the state appropriately.
  state->local->errnum = ERR_COMP_SYNTAX;
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
  memcpy(out->nextbyte, s, l); // Copy the string directly
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
  if (local->count >= MAX_LOCAL_VARS) {
    free(id);
    return false;
  }
  // Add the new local variable.
  local->id[local->count] = id;
  local->count++;
  return true;
}

void emit_local_assign(char *id, OUTPUT_t *out, LOCAL_t *local) {
  // prepare_local_assign() must be called before this!
  // Otherwise there will be no matching local variable.
  for (uint8_t l = 0; l < local->count; l++) {
    if (strcmp(id, local->id[l]) == 0) {
      // Local variable already exists, emit bytecode to assign it.
      emit_byte('c', out);
      emit_byte(l, out);
    }
  }
}

bool emit_local_op(char *id, LOCAL_t *local, OUTPUT_t *out, char op) {
  // Emit the specified local operation ('f' for increment, 'g'
  // for decrement), followed by the local to increment or decrement.
  emit_byte(op, out);
  if (emit_local_index(id, out, local)) {
    return true;
  } else {
    local->errnum = ERR_COMP_LOCALBEFOREDEF;
    return false;
  }
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

void emit_embedded_code(OUTPUT_t *out, char *code) {
  // Emit embedded code to be compiled by the interpreter.
  emit_byte('B', out);
  emit_string(code, out); // Aaaand the code.
}

void merge_item_buffer(OUTPUT_t *out, OUTPUT_t *item_out) {
  // Items are assembled in a separate buffer, to facilitate the passing
  // of arguments.  This function merges the item buffer into the main
  // output buffer.

  // Check if there is enough space in the buffer
  int mainbuf_size = out->nextbyte - out->bytecode;
  int itembuf_size = item_out->nextbyte - item_out->bytecode;
  if (mainbuf_size + itembuf_size >= out->maxsize) {
    // Calculate the new buffer size
    int oldsize = out->maxsize;
    out->maxsize = GROW_CAPACITY((mainbuf_size + itembuf_size) * 1.25);
    // Reallocate the buffer
    unsigned char *new_buffer = GROW_ARRAY(unsigned char, out->bytecode, oldsize, out->maxsize);
    // Update buffer pointers
    out->nextbyte = new_buffer + (out->nextbyte - out->bytecode);
    out->bytecode = new_buffer;
  }
  // Now we are sure the buffer is big enough, smush the item buffer
  // into the main output buffer.
  memcpy(out->nextbyte, item_out->bytecode, itembuf_size);
  out->nextbyte += itembuf_size;
  // Reset the item buffer for its next use
  item_out->nextbyte = item_out->bytecode;
}

bool prepare_item(SCANNER_STATE_t *state) {
  // We have encountered the start of an item, so begin shoving it into
  // a temporary buffer until we've processed the arguments (if any).
  if (state->item_count >= MAX_NESTED_CONTROLS) {
    return false;
  }
  state->item_count++;
  int8_t c = state->item_count;
  state->arg_count[c] = 0;
  state->item_out[c] = GROW_ARRAY(OUTPUT_t, NULL, 0, sizeof(OUTPUT_t));
  state->item_out[c]->maxsize = 1024;
  state->item_out[c]->bytecode = GROW_ARRAY(unsigned char, NULL, 0,
                                              state->item_out[c]->maxsize);
  state->item_out[c]->nextbyte = state->item_out[c]->bytecode;
  state->item_buf = state->item_out[c];
  return true;
}

void finalise_item(SCANNER_STATE_t *state) {
  // Having processed the item, merge its buffer into the bytestream
  // and clean up after ourselves.
  int8_t c = state->item_count;
  merge_item_buffer(state->out, state->item_out[c]);
  FREE_ARRAY(unsigned char, state->item_out[c]->bytecode,
                                               state->item_out[c]->maxsize);
  FREE_ARRAY(OUTPUT_t, state->item_out[c], sizeof(OUTPUT_t));
  state->item_count--;
  if (state->item_count > -1) {
    state->item_buf = state->item_out[state->item_count];
  } else {
    state->item_buf = NULL;
  }
}

void cleanup_item(SCANNER_STATE_t *state) {
  // Called when there is a failed parse to clean up any item buffers
  // which may have been allocated.
  int8_t c = state->item_count;
  while (c >= 0) {
    FREE_ARRAY(unsigned char, state->item_out[c]->bytecode,
                                               state->item_out[c]->maxsize);
    FREE_ARRAY(OUTPUT_t, state->item_out[c], sizeof(OUTPUT_t));
    c--;
  }
}

bool parse_source(char *source, int sourcelen, OUTPUT_t *out,
                                                          LOCAL_t *local) {
  // source holds the source input string
  // sourcelen holds the length of the input
  // out is a pointer to a struct which holds the output buffer
  yyscan_t sc;
  yy_extra_type my_extra;
  my_extra.deref_depth = 0;

  // Wrap all these bits of state up into a nice package
  // for ease of transport
  SCANNER_STATE_t scanner_state;
  scanner_state.out = out;
  scanner_state.local = local;
  scanner_state.local->errnum = ERR_NOERROR;
  scanner_state.control_count = -1; // We start in no loop.
  scanner_state.item_count = -1; // We start processing no item.
  scanner_state.item_buf = NULL;

  yylex_init_extra(my_extra, &sc);
  FILE *in = fmemopen(source, sourcelen, "r");
  yyset_in(in, sc);

  // Before we start parsing, make space in the output for the number
  // of locals - we will backfill the actual number later.
  emit_byte(0, out);
  // And the number of parameters this item can receive.
  // NOTE! Parameters are treated internally as local variables, so the
  // number of local variables will INCLUDE the number of parameters.
  emit_byte(0, out);

  bool failed = yyparse(sc, &scanner_state);

  // Clean up
  fclose(in);
  yylex_destroy(sc);

  if (!failed) {
    // ...and now we know how many locals there are, update the first
    // two bytes of the bytecode.
    out->bytecode[0] = local->count;
    out->bytecode[1] = local->param_count;
    return true;
  } else {
    cleanup_item(&scanner_state);
    return false;
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
%nonassoc TEXISTS TDELETE
%right UMINUS TNOT
%nonassoc TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA

%destructor { free ($$); } <*>

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
                  state->local->errnum = ERR_COMP_MAXDEPTH;
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
        | TRETURN { emit_byte('h', state->out); }
        | TLOCAL TASSIGN { if (!prepare_local_assign($1, state->out,
                                                      state->local)) {
                           state->local->errnum = ERR_COMP_TOOMANYLOCALS;
                           YYERROR; }
                                                                                                     } expr { emit_local_assign($1, state->out, state->local); }
        | complete_item TASSIGN item_assignment
        | TLOCAL TINC   { bool tf = emit_local_op($1, state->local,
                                                          state->out, 'f');
                          free($1);
                          if (!tf) YYERROR; }
        | TLOCAL TDEC   { bool tf = emit_local_op($1, state->local,
                                                          state->out, 'g');
                          free($1);
                          if (!tf) YYERROR; }
        | expr                  { }
        ;

expr:     TLOCAL        { bool tf = emit_local_op($1, state->local,
                                                          state->out, 'e');
                          free($1); 
                          if (!tf) YYERROR; }
        |	TINTEGER      { emit_byte('p', state->out);
                          emit_int64(atoi($1), state->out);
                          free($1); }
        |	TSTRINGLIT    { emit_byte('l', state->out);
                          emit_string($1, state->out);
                          free($1); }
        |	item args { finalise_item(state);
                      emit_byte('E', state->out);
                      emit_byte('F', state->out);
                      /* Use item_count + 1 below because finalise_item()
                         decremented it. */
                      emit_int16(state->arg_count[state->item_count + 1],
                                                              state->out); }
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
        | funcop
        | libcall
        | TUNKNOWNCHAR          {
                                  state->local->errnum =
                                                      ERR_COMP_UNKNOWNCHAR;
                                  free($1);
                                  YYERROR;
                                }
        ;

funcop:   TEXISTS TLBRACE complete_item TRBRACE { emit_byte('X',
                                                             state->out); }
funcop:   TDELETE TLBRACE complete_item TRBRACE { emit_byte('W',
                                                             state->out); }
        ;

libcall:  TLIBNAME TLAYERSEP TLAYER { prepare_item(state); }
                       args { uint8_t lib, call, args;
                         uint8_t arg_count =
                                       state->arg_count[state->item_count];
                         if (libcall_lookup($1, $3, &lib, &call, &args)) {
                           free($1); free($3);
                           if (arg_count != args) {
                             state->local->errnum = ERR_COMP_WRONGARGS;
                             YYERROR;
                           }
                           finalise_item(state);
                           emit_byte('A', state->out);
                           emit_byte(lib, state->out);
                           emit_byte(call, state->out);
                         } else {
                           state->local->errnum =
                                           ERR_COMP_UNKNOWNLIB;
                           free($1); free($3); YYERROR;
                         }
                       }
        ;

elsif_else_opt: /* empty */
        | TELSIF { fixup_last_else_jump(state); }
          expr { emit_jump_to_next_else(state); }
          TTHEN stmtlist { emit_jump_to_endif(state); } elsif_else_opt
        | TELSE { fixup_last_else_jump(state); } stmtlist
        ;

params:
        | TLBRACE { emit_byte('P', state->out); }
          param_list TRBRACE { emit_int16(0, state->out); }
        ;

param_list: param_local
        | param_local TCOMMA param_list
        ;

param_local: TLOCAL { emit_string($1, state->out); free($1); }
        ;

args:
        | TLBRACE arg_list TRBRACE
        ;

arg_list: expr { state->arg_count[state->item_count]++; }
        | expr { state->arg_count[state->item_count]++; } TCOMMA arg_list
        ;

item_assignment: expr { emit_byte('C', state->out); }
        | TCODE { emit_byte('B', state->out); }
          params TCODEBODY { emit_string($4, state->out); free($4); }
        ;

complete_item: item { finalise_item(state); emit_byte('E', state->out); }
        ;

item:   first_layer
        | item TLAYERSEP layer
        ;

first_layer: { prepare_item(state); emit_byte('I', state->item_buf); } layer
        ;

layer:  TLAYER { emit_byte('L', state->item_buf);
                 emit_layer($1, state->item_buf); free($1); }
        | dereference
        ;

dereference:  TDEREFSTART { emit_byte('D', state->item_buf); }
              deref_content TDEREFEND
        ;

deref_content: TLOCAL { bool tf = emit_local_op($1, state->local,
                                                    state->item_buf, 'V');
                        free($1);
                        if (!tf) YYERROR; }
        | item        { emit_byte('E', state->item_buf); }
        ;

%%

