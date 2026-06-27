// The interpreter

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "config.h"
#include "error.h"
#include "util.h"
#include "interpret.h"
#include "libcall.h"
#include "log.h"
#include "memory.h"
#include "parser.h"
#include "compiler_pipeline.h"
#include "compiler/ir/opcode_schema.h"
#include "absyn.h"
#include "value.h"
#include "stack.h"
#include "item.h"

// The configuration object, defined in sin.c
extern CONFIG_t config;

static VM_t *current_vm = NULL;
#define VM current_vm
static uint8_t *current_frame_start = NULL;
static uint8_t *current_frame_end = NULL;
static ITEM_t *current_item = NULL;
static ITEM_t *pending_call_item = NULL;

static inline bool require_bytes(uint8_t *nextop, size_t bytes, const char *opname) {
  if (!current_frame_start || !current_frame_end) return true;
  if (nextop > current_frame_end || (size_t)(current_frame_end - nextop) < bytes) {
    char detail[128];
    snprintf(detail, sizeof(detail), "%s truncated bytecode read (%zu bytes)", opname, bytes);
    logerr("%s.\n", detail);
    set_error_item(ERR_RUNTIME_TRUNCATED, detail);
    return false;
  }
  return true;
}

#define REQUIRE_BYTES(nextop, n, opname) \
  do { if (!require_bytes((nextop), (n), (opname))) return NULL; } while (0)

static uint8_t *bc_read_u8(uint8_t *nextop, uint8_t *out, const char *opname) {
  REQUIRE_BYTES(nextop, sizeof(*out), opname);
  memcpy(out, nextop, sizeof(*out));
  return nextop + sizeof(*out);
}

static uint8_t *bc_read_u16(uint8_t *nextop, uint16_t *out, const char *opname) {
  REQUIRE_BYTES(nextop, sizeof(*out), opname);
  memcpy(out, nextop, sizeof(*out));
  return nextop + sizeof(*out);
}

static uint8_t *bc_read_i16(uint8_t *nextop, int16_t *out, const char *opname) {
  REQUIRE_BYTES(nextop, sizeof(*out), opname);
  memcpy(out, nextop, sizeof(*out));
  return nextop + sizeof(*out);
}

static uint8_t *bc_read_u64_payload(uint8_t *nextop, uint64_t *out, const char *opname) {
  REQUIRE_BYTES(nextop, sizeof(*out), opname);
  memcpy(out, nextop, sizeof(*out));
  return nextop + sizeof(*out);
}

static uint8_t *bc_read_i64(uint8_t *nextop, int64_t *out, const char *opname) {
  uint64_t payload;
  uint8_t *after = bc_read_u64_payload(nextop, &payload, opname);
  if (!after) return NULL;
  memcpy(out, &payload, sizeof(*out));
  return after;
}

static OP_t opcode[256];
static bool interpreter_initialized = false;
uint8_t *op_undefined(uint8_t *nextop, ITEM_t *item);
typedef struct {
  OP_t *table;
} RuntimeBindingCheckCtx;

static bool assert_runtime_binding(uint8_t opbyte, IR_Op op, const IR_OpSchema *schema, void *raw_ctx) {
  (void)op;
  (void)schema;
  RuntimeBindingCheckCtx *check_ctx = (RuntimeBindingCheckCtx *)raw_ctx;
  assert(check_ctx->table[opbyte] != op_undefined
         && "Missing interpreter handler for schema-defined runtime opcode");
  return true;
}

typedef struct strbuf_meta {
  char *ptr;
  size_t cap;
  struct strbuf_meta *next;
} strbuf_meta_t;

static strbuf_meta_t *strbuf_head = NULL;

static strbuf_meta_t *strbuf_find(char *ptr) {
  strbuf_meta_t *meta = strbuf_head;
  while (meta) {
    if (meta->ptr == ptr) return meta;
    meta = meta->next;
  }
  return NULL;
}

static void strbuf_forget(char *ptr) {
  strbuf_meta_t **scan = &strbuf_head;
  while (*scan) {
    if ((*scan)->ptr == ptr) {
      strbuf_meta_t *found = *scan;
      *scan = found->next;
      free(found);
      return;
    }
    scan = &((*scan)->next);
  }
}

static void strbuf_track(char *ptr, size_t cap) {
  strbuf_meta_t *meta = strbuf_find(ptr);
  if (!meta) {
    meta = malloc(sizeof(strbuf_meta_t));
    if (!meta) return;
    meta->next = strbuf_head;
    strbuf_head = meta;
  }
  meta->ptr = ptr;
  meta->cap = cap;
}

static void free_runtime_string(char *s) {
  if (!s) return;
  strbuf_forget(s);
  free(s);
}

static void value_free_runtime(VALUE_t *value) {
  if (!value) return;
  if (value->type == VALUE_str) {
    free_runtime_string(value->s);
    value->type = VALUE_nil;
    value->i = 0;
  } else {
    value_free(value);
  }
}

static size_t strbuf_growth_capacity(size_t needed) {
  size_t cap = 16;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2) return needed;
    cap *= 2;
  }
  return cap;
}

static VALUE_t concat_two_strings(VALUE_t left, VALUE_t right) {
  size_t left_len = strlen(left.s);
  size_t right_len = strlen(right.s);
  size_t needed = left_len + right_len + 1;
  strbuf_meta_t *left_meta = strbuf_find(left.s);
  char *out = NULL;
  size_t out_cap = needed;

  if (left_meta && left_meta->cap >= needed) {
    out = left.s;
    memcpy(out + left_len, right.s, right_len + 1);
    out_cap = left_meta->cap;
    strbuf_forget(right.s);
    free(right.s);
  } else {
    if (left_meta) {
      out_cap = strbuf_growth_capacity(needed);
    }
    out = GROW_ARRAY(char, NULL, 0, out_cap);
    memcpy(out, left.s, left_len);
    memcpy(out + left_len, right.s, right_len + 1);
    free_runtime_string(left.s);
    free_runtime_string(right.s);
  }

  strbuf_track(out, out_cap);
  left.s = out;
  left.type = VALUE_str;
  return left;
}

static void log_invalid_binary_operands(const char *opcode_name,
                                        const VALUE_t *left,
                                        const VALUE_t *right) {
  logerr("%s invalid operand types: left '%s', right '%s'.\n",
         opcode_name, value_type_name(left ? left->type : VALUE_nil),
         value_type_name(right ? right->type : VALUE_nil));
}

typedef enum {
  CMP_EQ,
  CMP_NE,
  CMP_LT,
  CMP_LTE,
  CMP_GT,
  CMP_GTE,
} CMP_MODE_t;

static inline int pop_compare_and_push_bool(VM_t *vm, CMP_MODE_t mode, const char *opcode_tag) {
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  VALUE_t result = {VALUE_bool, {.i = 0}};
  VALUE_e v1_type = v1.type;
  VALUE_e v2_type = v2.type;
  (void)v1_type;
  (void)v2_type;

  switch (mode) {
    case CMP_EQ:
      result.i = value_equal(&v2, &v1);
      break;
    case CMP_NE:
      result.i = value_not_equal(&v2, &v1);
      break;
    case CMP_LT:
      result.i = value_less_than(&v2, &v1);
      break;
    case CMP_LTE:
      result.i = value_less_equal(&v2, &v1);
      break;
    case CMP_GT:
      result.i = value_greater_than(&v2, &v1);
      break;
    case CMP_GTE:
      result.i = value_greater_equal(&v2, &v1);
      break;
  }

  value_free_runtime(&v1);
  value_free_runtime(&v2);
  push_stack(VM->stack, result);
  if (!result.i) {
    DISASS_LOG("%s: types %d and %d\n", opcode_tag, v1_type, v2_type);
  }
  return result.i;
}


#define RUNTIME_OPCODE_TABLE(OP) \
  OP(0, op_nop) \
  OP('a', op_add) \
  OP('b', op_pushbool) \
  OP('c', op_savelocal) \
  OP('d', op_divide) \
  OP('e', op_getlocal) \
  OP('f', op_inclocal) \
  OP('g', op_declocal) \
  OP('j', op_jump) \
  OP('k', op_jumpfalse) \
  OP('l', op_pushstr) \
  OP('m', op_multiply) \
  OP('n', op_negate) \
  OP('o', op_equal) \
  OP('p', op_pushint) \
  OP('P', op_pushfloat) \
  OP('q', op_notequal) \
  OP('r', op_lessthan) \
  OP('s', op_subtract) \
  OP('t', op_greaterthan) \
  OP('u', op_lessthanorequal) \
  OP('v', op_greaterthanorequal) \
  OP('x', op_logicalnot) \
  OP('y', op_logicaland) \
  OP('z', op_logicalor) \
  OP('M', op_libcall_token) \
  OP('B', op_assigncodeitem) \
  OP('C', op_assignitem) \
  OP('F', op_fetchitem) \
  OP('I', op_assembleitem) \
  OP('R', op_assembleitem_rel) \
  OP('W', op_delete) \
  OP('X', op_exists) \
  OP('Y', op_nthname) \
  OP('Z', op_rootname)

uint8_t *op_nop(uint8_t *nextop, ITEM_t *item) {
  return nextop;
}

uint8_t *op_undefined(uint8_t *nextop, ITEM_t *item) {
  logerr("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

uint8_t *op_pushint(uint8_t *nextop, ITEM_t *item) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  VALUE_t v;
  v.type = VALUE_int;
  nextop = bc_read_i64(nextop, &v.i, "OP_PUSHINT");
  if (!nextop) return NULL;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHINT: %ld\n", v.i);
  return nextop;
}

uint8_t *op_pushfloat(uint8_t *nextop, ITEM_t *item) {
  VALUE_t v;
  v.type = VALUE_float;
  uint64_t bits;
  nextop = bc_read_u64_payload(nextop, &bits, "OP_PUSHFLOAT");
  if (!nextop) return NULL;
  v.f = value_float_from_bits(bits);
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHFLOAT: %g (0x%016llx)\n", v.f, (unsigned long long)bits);
  return nextop;
}

uint8_t *op_pushbool(uint8_t *nextop, ITEM_t *item) {
  uint8_t raw;
  nextop = bc_read_u8(nextop, &raw, "OP_PUSHBOOL");
  if (!nextop) return NULL;
  VALUE_t v;
  v.type = VALUE_bool;
  v.i = (raw != 0) ? 1 : 0;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHBOOL: %ld\n", v.i);
  return nextop;
}

uint8_t *op_inclocal(uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, increment it.  Otherwise complain.
  uint8_t index = *nextop + VM->stack->base;
  if (VM->stack->stack[index].type == VALUE_int) {
    VM->stack->stack[index].i++;
  } else {
    logerr("Trying to increment non integer local variable.\n");
  }
  DISASS_LOG("OP_INCLOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_declocal(uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, decrement it.  Otherwise complain.
  uint8_t index = *nextop + VM->stack->base;
  if (VM->stack->stack[index].type == VALUE_int) {
    VM->stack->stack[index].i--;
  } else {
    logerr("Trying to decrement non integer local variable.\n");
  }
  DISASS_LOG("OP_DECLOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_jump(uint8_t *nextop, ITEM_t *item) {
  // Unconditional jump.  Interpret the next two bytes as a
  // SIGNED int, and then modify the bytecode pointer by that amount.
  int16_t offset;
  nextop = bc_read_i16(nextop, &offset, "OP_JUMP");
  if (!nextop) return NULL;
  DISASS_LOG("OP_JUMP: offset is  %d.\n", offset);
  return nextop - sizeof(offset) + offset;
}

uint8_t *op_jumpfalse(uint8_t *nextop, ITEM_t *item) {
  // Evaluate the top of the stack.  If false, interpret next
  // two bytes as a SIGNED int, and modify the bytecode pointer
  // by that amount.  Alternatively, if true, simply skip the next
  // two bytes and go on to the next instruction.

  int16_t offset;
  uint8_t *offset_start = nextop;
  nextop = bc_read_i16(nextop, &offset, "OP_JUMPFALSE");
  if (!nextop) return NULL;
  VALUE_t v1;
  v1 = pop_stack(VM->stack);
  if (value_is_truthy(&v1)) {
    // A true value means that we don't branch.  Skip over
    // the next two bytes.
    DISASS_LOG("OP_JUMPFALSE: evaluates to true (no jump).\n");
    value_free(&v1);
    return nextop;
  } else {
    // If not true then it must be false.  That's logic.
    DISASS_LOG("OP_JUMPFALSE: evaluates to false (jump offset %d).\n", offset);
    return offset_start + offset;
  }
}

uint8_t *op_savelocal(uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop + VM->stack->base;
  // First check if the current value is a string.  If so, free it.
  VALUE_t top = pop_stack(VM->stack);
  value_move(&VM->stack->stack[index], &top);
  DISASS_LOG("OP_SAVELOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_getlocal(uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop + VM->stack->base;

  push_stack(VM->stack, value_clone(&VM->stack->stack[index]));
#ifdef DISASS
  VALUE_t v;
  v = peek_stack(VM->stack);
  switch (v.type) {
    case VALUE_int:
      DISASS_LOG("OP_GETLOCAL: index %d value %d.\n", index, v.i);
      break;
    case VALUE_str:
      DISASS_LOG("OP_GETLOCAL: index %d value '%s'.\n", index, v.s);
      break;
    default:
      DISASS_LOG("OP_GETLOCAL: index %d type %d.\n", index, v.type);
  }
#endif
  return nextop+1;
}

uint8_t *op_pushstr(uint8_t *nextop, ITEM_t *item) {
  // Push a string literal onto the stack.
  VALUE_t v;
  v.type = VALUE_str;
  uint16_t len;
  // Get the length
  nextop = bc_read_u16(nextop, &len, "OP_PUSHSTR length");
  if (!nextop) return NULL;
  REQUIRE_BYTES(nextop, len, "OP_PUSHSTR payload");
  v.s = GROW_ARRAY(char, NULL, 0, len+1);
  memcpy(v.s, nextop, len);
  v.s[len] = 0;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHSTR: %s\n", v.s);
  return nextop + len;
}

uint8_t *op_add(uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack.  If both ints, add them and push the
  // result onto the stack.  If both strings, concatenate them and do same.
  // If disparate types, push NIL onto the stack.
  VALUE_t v1, v2;
  v1 = pop_stack(VM->stack);
  v2 = pop_stack(VM->stack);
  VALUE_t result;
  if (value_add(&v2, &v1, &result)) {
    value_free_runtime(&v1);
    value_free_runtime(&v2);
    push_stack(VM->stack, result);
  } else if (v1.type == VALUE_str && v2.type == VALUE_str) {
    push_stack(VM->stack, concat_two_strings(v2, v1));
  } else {
    VALUE_e left_type = v2.type;
    VALUE_e right_type = v1.type;
    value_free_runtime(&v1);
    value_free_runtime(&v2);
    logerr("OP_ADD invalid operand types: left '%s', right '%s'. Result is NIL.\n",
          value_type_name(left_type), value_type_name(right_type));
    push_stack(VM->stack, VALUE_NIL);
  }
  DISASS_LOG("OP_ADD: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_subtract(uint8_t *nextop, ITEM_t *item) {
  // Pop two values, subtract the last from the first, then push the result
  // onto the stack. If either of the values is not an int, the result
  // is nil.
  VALUE_t v1, v2;
  v1 = pop_stack(VM->stack);
  v2 = pop_stack(VM->stack);
  VALUE_t result;
  if (value_sub(&v2, &v1, &result)) {
    DISASS_LOG("OP_SUB: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_SUB", &v2, &v1);
    DISASS_LOG("OP_SUB: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free_runtime(&v1);
  value_free_runtime(&v2);
  push_stack(VM->stack, result);
  return nextop;
}

uint8_t *op_divide(uint8_t *nextop, ITEM_t *item) {
  // Pop two values, divide the last by the first, then push the result
  // onto the stack. If either of the values is not an int, the result
  // is nil.
  // Trap divide by zero and substitute a result of zero.
  VALUE_t v1, v2;
  v1 = pop_stack(VM->stack);
  v2 = pop_stack(VM->stack);
  VALUE_t result;
  if (value_div(&v2, &v1, &result)) {
    if (v1.type == VALUE_int && v1.i == 0) {
      logerr("Attempt to divide by zero.  Substitute zero as result.\n");
    }
    DISASS_LOG("OP_DIV: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_DIV", &v2, &v1);
    DISASS_LOG("OP_DIV: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free_runtime(&v1);
  value_free_runtime(&v2);
  push_stack(VM->stack, result);
  return nextop;
}

uint8_t *op_multiply(uint8_t *nextop, ITEM_t *item) {
  // Pop two values, multiply them together, then push the result onto the
  // stack.  If either of the values is not an int, the result is nil.
  VALUE_t v1, v2;
  v1 = pop_stack(VM->stack);
  v2 = pop_stack(VM->stack);
  VALUE_t result;
  if (value_mul(&v2, &v1, &result)) {
    DISASS_LOG("OP_MUL: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_MUL", &v2, &v1);
    DISASS_LOG("OP_MUL: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free_runtime(&v1);
  value_free_runtime(&v2);
  push_stack(VM->stack, result);
  return nextop;
}

uint8_t *op_negate(uint8_t *nextop, ITEM_t *item) {
  // If the top value on the stack is an int, negate it.
  //  Complain bitterly if not.
  if (!value_neg(&VM->stack->stack[VM->stack->current])) {
    logerr("Attempt to negate a value of type '%d'.\n",
                                 VM->stack->stack[VM->stack->current].type);
  }
  DISASS_LOG("OP_NEGATE: type %d\n", VM->stack->stack[VM->stack->current].type);
  return nextop;
}

uint8_t *op_equal(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_EQ, "OP_EQUAL");
  return nextop;
}

uint8_t *op_notequal(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_NE, "OP_NOTEQUAL");
  return nextop;
}

uint8_t *op_lessthan(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_LT, "OP_LESSTHAN");
  return nextop;
}

uint8_t *op_lessthanorequal(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_LTE, "OP_LTEQ");
  return nextop;
}

uint8_t *op_greaterthan(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_GT, "OP_GREATERTHAN");
  return nextop;
}

uint8_t *op_greaterthanorequal(uint8_t *nextop, ITEM_t *item) {
  pop_compare_and_push_bool(VM, CMP_GTE, "OP_GTEQ");
  return nextop;
}

uint8_t *op_logicalnot(uint8_t *nextop, ITEM_t *item) {
  // Logically negate the value on top of the stack.
  VALUE_t *v = &VM->stack->stack[VM->stack->current];
  value_to_bool_inplace(v);
  v->i = !v->i;
  return nextop;
}

uint8_t *op_logicaland(uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // AND the result and push it.
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  value_to_bool_inplace(&v1);
  value_to_bool_inplace(&v2);
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i && v2.i; // Logical AND
  push_stack(VM->stack, v2);
  return nextop;
}

uint8_t *op_logicalor(uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // OR the result and push it.
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  value_to_bool_inplace(&v1);
  value_to_bool_inplace(&v2);
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i || v2.i; // Logical OR
  push_stack(VM->stack, v2);
  return nextop;
}

uint8_t *op_libcall_token(uint8_t *nextop, ITEM_t *item) {
  uint8_t token = *nextop++;
  DISASS_LOG("Calling libcall token %d.\n", token);
  OP_t libcall = libcall_func_token(token);
  if (!libcall) {
    char detail[64];
    snprintf(detail, sizeof(detail), "Unknown libcall token %u", token);
    logerr("%s.\n", detail);
    set_error_item(ERR_RUNTIME_INVLIB, detail);
    push_stack(VM->stack, VALUE_NIL);
  } else {
    nextop = libcall(nextop, item);
  }
  return nextop;
}

void assignitem(VALUE_t *itemname, VALUE_t val) {
  // Given two values, use the first as the name of an item, and
  // the second as the value to assign to it.  The item name must be
  // freed after insertion.
  // If the itemname does not resolve into a valid item, this function
  // must fail silently (log messages are fine).  In this case, if the
  // value to be saved has memory allocated to it, that must be freed.
  // In other words, this is an end stage for values - they are either
  // used or discarded.  The interpreter no longer cares.
  if (itemname->type == VALUE_str) {
    ITEM_t *i = insert_item(config.itemroot, itemname->s, val);
    if (!i) {
      logerr("Unable to create item '%s'.\n", itemname->s);
      FREE_STR(val);
    }
    ITEMDEBUG_LOG("Saved value of type %d in item %s\n", val.type, itemname->s);
  } else {
    logerr("Unable to create item: invalid name type %d\n", itemname->type);
    FREE_STR(val);
  }
  FREE_STR(*itemname);
}

static bool canonicalize_itemname(const char *assembled_name, ITEM_t *current_item, char *out_name) {
  if (!assembled_name || assembled_name[0] == '\0') return false;

  if (assembled_name[0] == '.') {
    if (!current_item) {
      logerr("Relative item name '%s' cannot be resolved without current item context.\n", assembled_name);
      return false;
    }
    char parent[MAX_ITEM_NAME];
    get_itemname(current_item, parent);
    if (snprintf(out_name, MAX_ITEM_NAME, "%s%s", parent, assembled_name) >= MAX_ITEM_NAME) {
      logerr("Resolved item name exceeds MAX_ITEM_NAME: %s%s\n", parent, assembled_name);
      return false;
    }
    return true;
  }

  if (snprintf(out_name, MAX_ITEM_NAME, "%s", assembled_name) >= MAX_ITEM_NAME) {
    logerr("Item name exceeds MAX_ITEM_NAME: %s\n", assembled_name);
    return false;
  }
  return true;
}

typedef struct {
  char *buf;
  uint32_t cap;
  uint32_t len;
} STRBUILDER_t;

static void sb_init(STRBUILDER_t *sb, uint32_t cap) {
  sb->buf = GROW_ARRAY(char, NULL, 0, cap);
  sb->cap = cap;
  sb->len = 0;
  sb->buf[0] = '\0';
}

static void sb_ensure(STRBUILDER_t *sb, uint32_t add_len) {
  uint32_t need = sb->len + add_len + 1;
  if (need <= sb->cap) {
    return;
  }
  uint32_t new_cap = sb->cap;
  while (new_cap < need) {
    new_cap *= 2;
  }
  sb->buf = GROW_ARRAY(char, sb->buf, sb->cap, new_cap);
  sb->cap = new_cap;
}

static void sb_append_substr(STRBUILDER_t *sb, const char *src, uint32_t slen) {
  sb_ensure(sb, slen);
  memcpy(sb->buf + sb->len, src, slen);
  sb->len += slen;
  sb->buf[sb->len] = '\0';
}

static void sb_append_literal(STRBUILDER_t *sb, const char *literal) {
  sb_append_substr(sb, literal, (uint32_t)strlen(literal));
}

static void sb_append_intstr(STRBUILDER_t *sb, int64_t val) {
  char str[22];
  itoa(val, str, 10);
  sb_append_literal(sb, str);
}

typedef struct {
  const char **params;
  uint16_t *param_lens;
  size_t param_count;
  size_t total_param_len;
  char *source;
  uint16_t source_len;
} CODEITEM_INPUT_t;

static bool decode_assigncode_params(uint8_t **opcode, CODEITEM_INPUT_t *in) {
  // Format assumption for params block: <u16 len><bytes> repeated, terminated by <u16 0>.
  const size_t MAX_ASSIGNCODE_PARAMS = 1024;
  const size_t MAX_ASSIGNCODE_PARAM_BYTES = 65535;
  while (1) {
    REQUIRE_BYTES(*opcode, 2, "OP_ASSIGNCODEITEM param-len");
    uint16_t param_len = 0;
    memcpy(&param_len, *opcode, 2);
    *opcode += 2;
    if (param_len == 0) break;
    if (in->param_count >= MAX_ASSIGNCODE_PARAMS) return false;
    if ((in->total_param_len + param_len) > MAX_ASSIGNCODE_PARAM_BYTES) return false;
    REQUIRE_BYTES(*opcode, param_len, "OP_ASSIGNCODEITEM param-bytes");
    char *param = GROW_ARRAY(char, NULL, 0, param_len + 1);
    memcpy(param, *opcode, param_len);
    param[param_len] = '\0';
    *opcode += param_len;
    in->params = GROW_ARRAY(const char *, (char **)in->params, in->param_count, in->param_count + 1);
    in->param_lens = GROW_ARRAY(uint16_t, in->param_lens, in->param_count, in->param_count + 1);
    in->params[in->param_count] = param;
    in->param_lens[in->param_count] = param_len;
    in->param_count++;
    in->total_param_len += param_len;
  }
  return true;
}

static bool decode_assigncode_source(uint8_t **opcode, CODEITEM_INPUT_t *in) {
  REQUIRE_BYTES(*opcode, 2, "OP_ASSIGNCODEITEM source-len");
  memcpy(&in->source_len, *opcode, 2);
  *opcode += 2;
  REQUIRE_BYTES(*opcode, in->source_len, "OP_ASSIGNCODEITEM source-bytes");
  in->source = GROW_ARRAY(char, NULL, 0, in->source_len + 1);
  memcpy(in->source, *opcode, in->source_len);
  in->source[in->source_len] = '\0';
  *opcode += in->source_len;
  return true;
}

static int8_t compile_and_insert_codeitem(const VALUE_t *itemname, const CODEITEM_INPUT_t *in, char **errdetail) {
  ITEM_t *testitem = find_item(config.itemroot, itemname->s);
  if (testitem && testitem->inuse) return ERR_COMP_INUSE;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode_with_params(in->source, in->source_len, in->params, in->param_count, &out, errdetail);
  if (rc == 0 && out) {
    uint32_t len = out->nextbyte - out->bytecode;
    ITEM_t *inserted = insert_code_item(config.itemroot, itemname->s, len, out->bytecode);
    if (!inserted) rc = ERR_COMP_INUSE;
  }
  if (out) {
    if (rc != 0 && out->bytecode) FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
    FREE_ARRAY(OUTPUT_t, out, 1);
  }
  return rc;
}

static void persist_codeitem_source(const VALUE_t *itemname, const CODEITEM_INPUT_t *in) {
  ITEM_t *code_item = find_item(config.itemroot, itemname->s);
  STRBUILDER_t sb;
  sb_init(&sb, in->source_len + in->total_param_len + 16);
  if (in->param_count > 0) {
    sb_append_literal(&sb, "code {");
    for (size_t pc = 0; pc < in->param_count; pc++) {
      sb_append_literal(&sb, in->params[pc]);
      if (pc < (in->param_count - 1)) sb_append_literal(&sb, ", ");
    }
    sb_append_literal(&sb, "} (");
  } else {
    sb_append_literal(&sb, "code (");
  }
  sb_append_literal(&sb, in->source);
  sb_append_literal(&sb, ");\n");
  if (!save_itemsource(code_item, sb.buf)) {
    char fullname[MAX_ITEM_NAME];
    get_itemname(code_item, fullname);
    logerr("Source was not saved.\nItem: %s\n", fullname);
    logerr("Source:\n%s\n", sb.buf);
  }
  FREE_ARRAY(char, sb.buf, sb.cap);
}

uint8_t *op_assigncodeitem(uint8_t *nextop, ITEM_t *item) {
  // Bytecode format assumption:
  //   ['P' <u16 len><bytes>...<u16 0>] optional parameter block,
  //   followed by mandatory <u16 source_len><source bytes>.
  CODEITEM_INPUT_t in = {0};
  VALUE_t itemname = VALUE_NIL;
  int8_t result = ERR_COMP_UNKNOWN;
  char *errdetail = NULL;

  if (*nextop == 'P') {
    nextop++;
    if (!decode_assigncode_params(&nextop, &in)) {
      set_error_item(ERR_COMP_UNKNOWN, "Invalid parameter block in code assignment bytecode.");
      goto cleanup;
    }
  }

  itemname = pop_stack(VM->stack);
  if (!decode_assigncode_source(&nextop, &in)) {
    set_error_item(ERR_COMP_UNKNOWN, "Invalid source block in code assignment bytecode.");
    goto cleanup;
  }

  DISASS_LOG("Source to compile: %s\n", in.source);
  if (itemname.type != VALUE_str) {
    logerr("Unable to assign code item: invalid name type %d.\n", itemname.type);
    set_error_item(ERR_COMP_UNKNOWN, "Invalid item name type for code assignment.");
    goto cleanup;
  }

  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (!canonicalize_itemname(itemname.s, item, fullname)) {
      set_error_item(ERR_COMP_UNKNOWN, "Invalid item name for code assignment.");
      goto cleanup;
    }
    FREE_ARRAY(char, itemname.s, strlen(itemname.s) + 1);
    itemname.s = strdup(fullname);
  }

  result = compile_and_insert_codeitem(&itemname, &in, &errdetail);
  if (result == 0) {
    persist_codeitem_source(&itemname, &in);
    set_item(config.itemroot, "error", VALUE_NIL);
    set_item(config.itemroot, "error.msg", VALUE_NIL);
  } else {
    logerr("Compilation failed.\n");
    set_error_item(result, errdetail);
  }

cleanup:
  if (errdetail) FREE_ARRAY(char, errdetail, strlen(errdetail) + 1);
  if (in.source) FREE_ARRAY(char, in.source, in.source_len + 1);
  if (in.params) {
    for (size_t pc = 0; pc < in.param_count; pc++) {
      FREE_ARRAY(char, (char *)in.params[pc], in.param_lens[pc] + 1);
    }
    FREE_ARRAY(const char *, (char **)in.params, in.param_count);
  }
  if (in.param_lens) FREE_ARRAY(uint16_t, in.param_lens, in.param_count);
  FREE_STR(itemname);
  return nextop;
}

uint8_t *op_assignitem(uint8_t *nextop, ITEM_t *item) {
  // Save a value into an item.
  VALUE_t val = pop_stack(VM->stack); // value to be saved
  VALUE_t itemname = pop_stack(VM->stack); // Name of item to save into
  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (canonicalize_itemname(itemname.s, item, fullname)) {
      FREE_ARRAY(char, itemname.s, strlen(itemname.s) + 1);
      itemname.s = strdup(fullname);
    } else {
      logerr("Unable to create item '%s': failed to resolve canonical name.\n", itemname.s);
    }
  }
  assignitem(&itemname, val);
  return nextop;
}

uint8_t *op_fetchitem(uint8_t *nextop, ITEM_t *item) {
  // Fetch a value from an item, and push it onto the stack.
  // The item name is a string at the top of the stack.
  // If the item is a code item, it is executed and the result pushed
  // onto the stack.
  // If the item does not exist, nil is pushed onto the stack.

  // First, let's get the number of arguments passed to this item
  uint16_t arg_count;
  nextop = bc_read_u16(nextop, &arg_count, "OP_FETCHITEM arg-count");
  if (!nextop) return NULL;

  // Now the item name.
  VALUE_t itemname = pop_stack(VM->stack);

  // First check to see if there is a valid item to look up
  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (!canonicalize_itemname(itemname.s, item, fullname)) {
      logerr("Unable to fetch item '%s': failed to resolve canonical name.\n", itemname.s);
      while (arg_count > 0) {
        DEBUG_LOG("Discarding argument for invalid canonical fetch name.\n");
        throwaway_stack(VM->stack);
        arg_count--;
      }
      push_stack(VM->stack, VALUE_NIL);
      FREE_STR(itemname);
      return nextop;
    }
    ITEM_t *i = find_item_cached(config.itemroot, fullname, NULL);
    if (i) {
      ITEMDEBUG_LOG("Fetched item %s (called with %d arguments).\n", fullname, arg_count);
      // Just push the item value onto the stack.
      if (i->type == ITEM_value) {
        VALUE_t v;
        v.type = i->value.type;
        if (v.type == VALUE_str) {
          v.s = strdup(i->value.s);
        } else {
          v.i = i->value.i;
        }
        push_stack(VM->stack, v);
      } else {
        // Are there any arguments in excess of what this item takes?
        // If so, lose 'em.
        while (arg_count > i->bytecode[1]) {
          DEBUG_LOG("Popping unneeded argument.\n");
          throwaway_stack(VM->stack);
          arg_count--;
        }
        // Contrariwise, do we have fewer arguments than we should?
        while (arg_count < i->bytecode[1]) {
          DEBUG_LOG("Pushing additional nil-value argument.\n");
          push_stack(VM->stack, VALUE_NIL);
          arg_count++;
        }
        // Save our current caller continuation state.
        // We pass the number of arguments, so that the stack is
        // correctly adjusted to account for them at the top of the
        // current stack (they will be at the bottom of the frame for
        // the new item).
        push_callstack(VM, item, nextop, i->bytecode[1], current_frame_start, current_frame_end);
        // Invariant at call-entry:
        // - caller VM stack/base/locals/params are captured in callstack.
        // - caller continuation (item + nextop + bytecode bounds) is captured.
        // - interpreter loop must transfer control to callee without recursion.
        ITEMDEBUG_LOG("Executing item %s\n", i->name);
        pending_call_item = i;
        return NULL;
      }
    } else {
      // Item not found.
      ITEMDEBUG_LOG("Item '%s' not found.\n", fullname);
      // We need to lose any values on the stack which were passed as args.
        while (arg_count > 0) {
          DEBUG_LOG("Popping unneeded argument.\n");
          throwaway_stack(VM->stack);
          arg_count--;
        }
      push_stack(VM->stack, VALUE_NIL);
    }
    FREE_STR(itemname);
  } else {
    logerr("Unable to fetch item: invalid item type for name: %d.\n", itemname.type);
    while (arg_count > 0) {
      DEBUG_LOG("Discarding argument for invalid item fetch name type.\n");
      throwaway_stack(VM->stack);
      arg_count--;
    }
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *assembleitem_helper(uint8_t *nextop, ITEM_t *item, bool relative) {
  // Interpret the following bytecode as an item.  If an item can be
  // assembled, push the full item name onto the stack as a string.
  // Return a pointer to the bytecode after the item assembly.
  // May recurse - necessary for the handling of nested derefs.
  bool invalid = false;
  bool saw_missing_layer = false;
  bool saw_non_missing_layer = false;
  bool missing_layer_is_leading = false;
  bool missing_layer_possibly_leading = false;
  bool just_processed_layer = false;
  STRBUILDER_t sb;
  sb_init(&sb, 130);
  if (relative) {
    if (!item) {
      logerr("Relative item assembly requires current item context.\n");
      invalid = true;
    } else {
      char parent[MAX_ITEM_NAME];
      get_itemname(item, parent);
      sb_append_literal(&sb, parent);
      sb_append_literal(&sb, ".");
    }
  }

  while (!invalid) {
    REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM layer");
    if (*nextop == 'E') {
      break;
    }
    switch (*nextop++) {
      case 'L': {
        // Simple layer
        REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM layer length");
        int s = *nextop++; // Length of layer name
        REQUIRE_BYTES(nextop, s, "OP_ASSEMBLEITEM layer bytes");
        sb_append_substr(&sb, (char *)nextop, (uint32_t)s);
        nextop += s;
        saw_non_missing_layer = true;
        just_processed_layer = true;
        break;
      }
      case 'D': {
        // Deref layer - either a V (localvar) or another I (item)
        REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM deref type");
        uint8_t deref_type = *nextop++;
        switch (deref_type) {
          case 'V': {
            REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM local index");
            int idx = *nextop++ + VM->stack->base; // Local variable index
            switch (VM->stack->stack[idx].type) {
              case VALUE_str: {
                // This is easy, just concatenate the context of this local
                // Assuming it is a valid layer name, anyway.
                if (VM->stack->stack[idx].s[0] == '\0') {
                  if (!saw_non_missing_layer && !saw_missing_layer) {
                    saw_missing_layer = true;
                    missing_layer_possibly_leading = true;
                  } else {
                    logerr("Missing layer name in non-leading position.\n");
                    invalid = true;
                  }
                } else if (is_valid_layer(VM->stack->stack[idx].s)) {
                  sb_append_literal(&sb, VM->stack->stack[idx].s);
                  saw_non_missing_layer = true;
                } else {
                  logerr("Invalid layer name '%s'.\n", VM->stack->stack[idx].s);
                  invalid = true;
                }
                just_processed_layer = true;
                break;
              }
              case VALUE_int: {
                // Slightly more complicated.  Turn the int into a string.
                sb_append_intstr(&sb, VM->stack->stack[idx].i);
                saw_non_missing_layer = true;
                just_processed_layer = true;
                break;
              }
              case VALUE_float: {
                logerr("Float value cannot be used as an item layer name.\n");
                invalid = true;
                break;
              }
              case VALUE_nil: {
                if (!saw_non_missing_layer && !saw_missing_layer) {
                  saw_missing_layer = true;
                  missing_layer_possibly_leading = true;
                } else {
                  logerr("Missing layer name in non-leading position.\n");
                  invalid = true;
                }
                just_processed_layer = true;
                break;
              }
              default: {
                // Not a valid value type to convert into a layer name.
                logerr("Layer type (%d) not int or string.\n", VM->stack->stack[idx].type);
                invalid = true;
              }
            }
            break;
          }
          case 'I':
          case 'R': {
            // This is a bit more complicated.  We need to dereference an
            // item, then evaluate it, and use the result as the layer name.
            nextop = assembleitem_helper(nextop, item, deref_type == 'R');
            if (!nextop) {
              invalid = true;
              break;
            }
            VALUE_t layername = pop_stack(VM->stack);
            if (layername.type == VALUE_str) {
              //  This is basically the same as op_fetchitem
              ITEM_t *i = find_item(config.itemroot, layername.s);
              if (i) {
                // We have an item.  Only two value types are allowed.
                switch (i->value.type) {
                  case VALUE_str: {
                    // This is the easiest one
                    if (i->value.s[0] == '\0') {
                      if (!saw_non_missing_layer && !saw_missing_layer) {
                        saw_missing_layer = true;
                        missing_layer_possibly_leading = true;
                      } else {
                        logerr("Missing layer name in non-leading position.\n");
                        invalid = true;
                      }
                    } else if (is_valid_layer(i->value.s)) {
                      sb_append_literal(&sb, i->value.s);
                      saw_non_missing_layer = true;
                    } else {
                      logerr("Invalid layer name '%s'.\n", i->value.s);
                      invalid = true;
                    }
                    just_processed_layer = true;
                    break;
                  }
                  case VALUE_int: {
                    // This needs to be converted to a string.
                    sb_append_intstr(&sb, i->value.i);
                    saw_non_missing_layer = true;
                    just_processed_layer = true;
                    break;
                  }
                  case VALUE_float: {
                    logerr("Item dereference failed for '%s': float value cannot be used as an item layer name.\n",
                           layername.s);
                    invalid = true;
                    break;
                  }
                  case VALUE_nil: {
                    if (!saw_non_missing_layer && !saw_missing_layer) {
                      saw_missing_layer = true;
                      missing_layer_possibly_leading = true;
                    } else {
                      logerr("Missing layer name in non-leading position.\n");
                      invalid = true;
                    }
                    just_processed_layer = true;
                    break;
                  }
                  default: {
                    logerr("Item dereference failed for '%s': invalid type.\n", layername.s);
                    invalid = true;
                  }
                }
              } else {
                logerr("Item dereference failed for '%s'.\n", layername.s);
                invalid = true;
              }
              FREE_STR(layername);
            } else {
              logerr("Invalid item layer type %d.\n", layername.type);
              invalid = true;
            }
            break;
          }
          default: {
            logerr("Invalid dereference layer type '%c' (%d).\n", *nextop, *nextop);
            invalid = true;
          }
        }
        break;
      }
      default: {
        logerr("Invalid layer type '%c' (%d).\n", *nextop, *nextop);
        invalid = true;
      }
    }
    if (invalid) {
      break;
    }
    if (missing_layer_possibly_leading && saw_non_missing_layer) {
      missing_layer_is_leading = true;
      missing_layer_possibly_leading = false;
    }
    if (*nextop != 'E') {
      // Another layer to process, so add a dot separator.
      if (just_processed_layer && saw_non_missing_layer) {
        sb_append_literal(&sb, ".");
      } else if (just_processed_layer && saw_missing_layer && !saw_non_missing_layer) {
        // Leading missing layer candidate: don't emit a separator yet.
      }
    } else {
      // End of item definition.
      break;
    }
    just_processed_layer = false;
  }

  if (!invalid && saw_missing_layer) {
    if (!missing_layer_is_leading || !saw_non_missing_layer) {
      invalid = true;
    }
  }

  if (invalid) {
    // Not a valid item name, so push nil.
    FREE_ARRAY(char, sb.buf, sb.cap);
    push_stack(VM->stack, VALUE_NIL);
  } else {
    VALUE_t name;
    name.type = VALUE_str;
    name.s = sb.buf; // Don't free - it's on the stack!
    push_stack(VM->stack, name);
    ITEMDEBUG_LOG("Item assembled: %s\n", sb.buf);
  }
  REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM terminator");
  return nextop + 1;
}

uint8_t *op_assembleitem(uint8_t *nextop, ITEM_t *item) {
  // Here beginneth an item definition.  Items are made up of layers, and
  // each layer may be either a simple layer name (a string matching the
  // regexp [_a-z0-9]), or it may be a dereference.  Dereferences are
  // either items (which may also contain dereferences), or they are
  // local variables.  In both cases, once the local variable or the item
  // has been fully determined, it needs to be looked up, and the
  // content should be substituted into that layer.  Nil values and empty
  // strings are prohibited.

  // When this function is called, the I opcode has been eaten.

  // To facilitate ease of recusive dereferences, this is just a wrapper
  // to the help function which does all the work.
  nextop = assembleitem_helper(nextop, item, false);
  return nextop;
}

uint8_t *op_assembleitem_rel(uint8_t *nextop, ITEM_t *item) {
  nextop = assembleitem_helper(nextop, item, true);
  return nextop;
}

uint8_t *op_delete(uint8_t *nextop, ITEM_t *item) {
  // When this opcode is encountered, an item will previously have been
  // assembled and pushed onto the stack (or nil if the assembly failed).
  // Pop it, delete it, and return nothing.
  VALUE_t val = pop_stack(VM->stack);
  if (val.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (canonicalize_itemname(val.s, item, fullname)) {
      delete_item(config.itemroot, fullname);
    } else {
      logerr("OP_DELETE failed to resolve canonical name for '%s'.\n", val.s);
    }
  } else {
    logerr("OP_DELETE invalid item name type: %d. No action taken.\n", val.type);
  }
  FREE_STR(val);
  DISASS_LOG("OP_DELETE\n");
  return nextop;
}

uint8_t *op_exists(uint8_t *nextop, ITEM_t *item) {
  // When this opcode is encountered, an item will previously have been
  // assembled and pushed onto the stack (or nil if the assembly failed).
  // Pop whatever is on the stack and evaluate it.  Push
  // true or false, depending on the result.
  VALUE_t val = pop_stack(VM->stack);
  if (val.type != VALUE_str) {
    logerr("OP_EXISTS invalid item name type: %d. Returning false.\n", val.type);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }
  char fullname[MAX_ITEM_NAME];
  if (!canonicalize_itemname(val.s, item, fullname)) {
    FREE_STR(val);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }
  ITEM_t *i = find_item(config.itemroot, fullname);
  FREE_STR(val);
  push_stack(VM->stack, i ? VALUE_TRUE : VALUE_FALSE);
  DISASS_LOG("OP_EXISTS\n");
  return nextop;
}

uint8_t *op_nthname(uint8_t *nextop, ITEM_t *item) {
  // This is a very inefficient way to iterate over all the children
  // of an item.  Pop the index, then pop the item.  Find the indexed
  // child of the item and return its name as a string.  If the child
  // is not found, return nil.  There has to be a better way.
  VALUE_t index = pop_stack(VM->stack);
  VALUE_t itemname = pop_stack(VM->stack);
  bool found = false;
  if (index.type == VALUE_int && index.i >= 0 && itemname.type == VALUE_str) {
    ITEM_t *i = find_item(config.itemroot, itemname.s);
    if (i) {
      ITEM_t *child = find_item_by_index(i, index.i);
      if (child) {
        found = true;
        VALUE_t result = {VALUE_str, {0}};
        result.s = strdup(child->name);
        push_stack(VM->stack, result);
      }
    }
  }
  if (!found) {
    push_stack(VM->stack, VALUE_NIL);
  }
  FREE_STR(index);
  FREE_STR(itemname);
  return nextop;
}

uint8_t *op_rootname(uint8_t *nextop, ITEM_t *item) {
  // Identical to op_nthname, except it only pops an index from the stack
  // and then uses it to index the itemroot.
  VALUE_t index = pop_stack(VM->stack);
  bool found = false;
  if (index.type == VALUE_int && index.i >= 0) {
    ITEM_t *child = find_item_by_index(config.itemroot, index.i);
    if (child) {
      found = true;
      VALUE_t result = {VALUE_str, {0}};
      result.s = strdup(child->name);
      push_stack(VM->stack, result);
    }
  }
  if (!found) {
    push_stack(VM->stack, VALUE_NIL);
  }
  FREE_STR(index);
  return nextop;
}

void init_interpreter() {
  libcall_registry_self_check(libcalls, true);
  if (!libcall_init_registry()) {
    logerr("Failed to initialize libcall registry.\n");
  }
  // This function simply sets up the opcode dispatch table.
  for (int o=0; o<256; o++) {
    opcode[o] = op_undefined;
  }
#define BIND_RUNTIME_OPCODE(opcode_byte, handler_fn) \
  opcode[(uint8_t)(opcode_byte)] = handler_fn;
  RUNTIME_OPCODE_TABLE(BIND_RUNTIME_OPCODE)
#undef BIND_RUNTIME_OPCODE

#ifndef NDEBUG
  RuntimeBindingCheckCtx ctx = {opcode};
  // NDEBUG disables assert(); keep this validation in debug builds so missing
  // runtime opcode bindings fail fast during development without affecting
  // optimized/release startup behavior.
  ir_opcode_schema_for_each_runtime_opcode(assert_runtime_binding, &ctx);
#endif
}

VALUE_t interpret(ITEM_t *item) {
  VM_t *vm = config.vm;
  if (!interpreter_initialized) {
    init_interpreter();
    interpreter_initialized = true;
  }
  current_vm = vm;
  set_item(config.itemroot, "error", VALUE_NIL);
  set_item(config.itemroot, "error.msg", VALUE_NIL);
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  current_item = item;
  pending_call_item = NULL;

  // Enter initial frame.
  current_item->inuse = true;
  VM->stack->current += current_item->bytecode[0] - current_item->bytecode[1];
  VM->stack->locals = current_item->bytecode[0];
  VM->stack->params = current_item->bytecode[1];
  uint8_t *op = current_item->bytecode + 2;
  current_frame_start = op;
  current_frame_end = current_item->bytecode + current_item->bytecode_len;

  while (true) {
    if (*op == 'h') {
      VALUE_t return_value = (size_stack(VM->stack) > 0) ? pop_stack(VM->stack) : VALUE_NIL;
      current_item->inuse = false;

      if (size_callstack(VM->callstack) == 0) {
        current_frame_start = NULL;
        current_frame_end = NULL;
        current_item = NULL;
        return return_value;
      }

      FRAME_t *prev_frame = pop_callstack(VM);
      // Invariant at return:
      // - pop_callstack restored caller stack/base/locals/params.
      // - caller continuation state tells us exactly where to resume.
      current_item = prev_frame->item;
      current_item->inuse = true;
      op = prev_frame->nextop;
      current_frame_start = prev_frame->bytecode_start;
      current_frame_end = prev_frame->bytecode_end;
      push_stack(VM->stack, return_value);
      continue;
    }

    uint8_t *nextop = op + 1;
    uint8_t *newop = opcode[*op](nextop, current_item);
    if (!newop) {
      if (pending_call_item) {
        current_item = pending_call_item;
        pending_call_item = NULL;
        current_item->inuse = true;
        VM->stack->current += current_item->bytecode[0] - current_item->bytecode[1];
        VM->stack->locals = current_item->bytecode[0];
        VM->stack->params = current_item->bytecode[1];
        op = current_item->bytecode + 2;
        current_frame_start = op;
        current_frame_end = current_item->bytecode + current_item->bytecode_len;
        continue;
      }
      current_item->inuse = false;
      current_frame_start = NULL;
      current_frame_end = NULL;
      current_item = NULL;
      return VALUE_NIL;
    }
    op = newop;
  }
}
