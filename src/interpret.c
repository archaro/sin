// The interpreter

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "error.h"
#include "util.h"
#include "interpret.h"
#include "libcall.h"
#include "log.h"
#include "memory.h"
#include "parser.h"
#include "compiler_pipeline.h"
#include "bytecode_verify.h"
#include "value.h"
#include "stack.h"
#include "item.h"
#include "runtime_decode.h"
#include "runtime_item_ops.h"
#include "runtime_opcode.h"
#include "runtime_value.h"

#define VM ctx->vm

bool runtime_init(RuntimeContext *ctx, VM_t *vm) {
  if (!ctx) return false;
  memset(ctx, 0, sizeof(*ctx));
  ctx->vm = vm;
  libcall_registry_self_check(libcalls, true);
  if (!libcall_registry_init(&ctx->libcalls)) {
    logerr("Failed to initialize libcall registry.\n");
    return false;
  }
  runtime_opcode_bind_table(ctx);
  ctx->initialized = true;
  return true;
}

void runtime_destroy(RuntimeContext *ctx) {
  if (!ctx) return;
  libcall_registry_destroy(&ctx->libcalls);
  memset(ctx->opcode, 0, sizeof(ctx->opcode));
  ctx->initialized = false;
}

void runtime_context_init(RuntimeContext *ctx, VM_t *vm) {
  (void)runtime_init(ctx, vm);
}

static const char *runtime_item_label(ITEM_t *item, char *buffer, size_t size) {
  if (!item) return "<null>";
  if (item->parent && item->parent->parent && size >= MAX_ITEM_NAME) {
    buffer[0] = '\0';
    get_itemname(item, buffer);
    return buffer;
  }
  return item->name;
}

static bool verify_runtime_bytecode(RuntimeContext *ctx, ITEM_t *item) {
  if (!ctx->strict_validation || !item || item->type != ITEM_code) return true;

  char item_name[MAX_ITEM_NAME] = {0};
  const char *label = runtime_item_label(item, item_name, sizeof(item_name));
  if (!item->bytecode) {
    char detail[320];
    snprintf(detail, sizeof(detail),
             "Runtime bytecode validation failed for item '%s' at offset 0: null bytecode pointer",
             label);
    logerr("%s.\n", detail);
    set_error_item(ERR_RUNTIME_BYTECODE, detail);
    return false;
  }
  BC_VerifyOptions options = bc_verify_strict_options();
  BC_VerifyResult result = bc_verify_bytecode(item->bytecode,
      (uint32_t)item->bytecode_len, label, &options);
  if (result.status == BC_VERIFY_OK) return true;

  char detail[320];
  snprintf(detail, sizeof(detail),
           "Runtime bytecode validation failed for item '%s' at offset %u: %s",
           label, result.diagnostic.offset, result.diagnostic.message);
  logerr("%s.\n", detail);
  set_error_item(ERR_RUNTIME_BYTECODE, detail);
  return false;
}

static bool report_decode_status(RuntimeDecodeStatus status) {
  if (runtime_decode_status_ok(status)) return true;
  if (status.code == RUNTIME_DECODE_TRUNCATED) {
    logerr("%s.\n", status.detail);
    set_error_item(ERR_RUNTIME_TRUNCATED, status.detail);
  }
  return false;
}

static uint8_t *decode_next(RuntimeDecodeStatus status) {
  return report_decode_status(status) ? status.next : NULL;
}

#define REQUIRE_BYTES(nextop, n, opname) \
  do { if (!report_decode_status(require_bytes(&ctx->decoder, (nextop), (n), (opname)))) return NULL; } while (0)

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
  VALUE_t v1 = pop_stack(vm->stack);
  VALUE_t v2 = pop_stack(vm->stack);
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
  push_stack(vm->stack, result);
  (void)opcode_tag;
  if (!result.i) {
    DISASS_LOG("%s: types %d and %d\n", opcode_tag, v1_type, v2_type);
  }
  return result.i;
}


uint8_t *op_nop(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return nextop;
}

uint8_t *op_undefined(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  logerr("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

uint8_t *op_pushint(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  (void)item;
  VALUE_t v;
  v.type = VALUE_int;
  nextop = decode_next(bc_read_i64(&ctx->decoder, nextop, &v.i, "OP_PUSHINT"));
  if (!nextop) return NULL;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHINT: %ld\n", v.i);
  return nextop;
}

uint8_t *op_pushfloat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t v;
  v.type = VALUE_float;
  uint64_t bits;
  nextop = decode_next(bc_read_u64_payload(&ctx->decoder, nextop, &bits, "OP_PUSHFLOAT"));
  if (!nextop) return NULL;
  v.f = value_float_from_bits(bits);
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHFLOAT: %g (0x%016llx)\n", v.f, (unsigned long long)bits);
  return nextop;
}

uint8_t *op_pushbool(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  uint8_t raw;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &raw, "OP_PUSHBOOL"));
  if (!nextop) return NULL;
  VALUE_t v;
  v.type = VALUE_bool;
  v.i = (raw != 0) ? 1 : 0;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHBOOL: %ld\n", v.i);
  return nextop;
}

uint8_t *op_inclocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, increment it.  Otherwise complain.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_INCLOCAL"));
  if (!nextop) return NULL;
  uint8_t index = raw_index + VM->stack->base;
  if (VM->stack->stack[index].type == VALUE_int) {
    VM->stack->stack[index].i++;
  } else {
    logerr("Trying to increment non integer local variable.\n");
  }
  DISASS_LOG("OP_INCLOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_declocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, decrement it.  Otherwise complain.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_DECLOCAL"));
  if (!nextop) return NULL;
  uint8_t index = raw_index + VM->stack->base;
  if (VM->stack->stack[index].type == VALUE_int) {
    VM->stack->stack[index].i--;
  } else {
    logerr("Trying to decrement non integer local variable.\n");
  }
  DISASS_LOG("OP_DECLOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_jump(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Unconditional jump.  Interpret the next two bytes as a
  // SIGNED int, and then modify the bytecode pointer by that amount.
  (void)item;
  int16_t offset;
  nextop = decode_next(bc_read_i16(&ctx->decoder, nextop, &offset, "OP_JUMP"));
  if (!nextop) return NULL;
  DISASS_LOG("OP_JUMP: offset is  %d.\n", offset);
  return nextop - sizeof(offset) + offset;
}

uint8_t *op_jumpfalse(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Evaluate the top of the stack.  If false, interpret next
  // two bytes as a SIGNED int, and modify the bytecode pointer
  // by that amount.  Alternatively, if true, simply skip the next
  // two bytes and go on to the next instruction.

  (void)item;
  int16_t offset;
  uint8_t *offset_start = nextop;
  nextop = decode_next(bc_read_i16(&ctx->decoder, nextop, &offset, "OP_JUMPFALSE"));
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
    value_free(&v1);
    return offset_start + offset;
  }
}

uint8_t *op_savelocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_SAVELOCAL"));
  if (!nextop) return NULL;
  uint8_t index = raw_index + VM->stack->base;
  // First check if the current value is a string.  If so, free it.
  VALUE_t top = pop_stack(VM->stack);
  value_move(&VM->stack->stack[index], &top);
  DISASS_LOG("OP_SAVELOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_getlocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_GETLOCAL"));
  if (!nextop) return NULL;
  uint8_t index = raw_index + VM->stack->base;

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
  return nextop;
}

uint8_t *op_pushstr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Push a string literal onto the stack.
  (void)item;
  VALUE_t v;
  v.type = VALUE_str;
  uint16_t len;
  // Get the length
  nextop = decode_next(bc_read_u16(&ctx->decoder, nextop, &len, "OP_PUSHSTR length"));
  if (!nextop) return NULL;
  REQUIRE_BYTES(nextop, len, "OP_PUSHSTR payload");
  v.s = malloc((size_t)len + 1);
  memcpy(v.s, nextop, len);
  v.s[len] = 0;
  push_stack(VM->stack, v);
  DISASS_LOG("OP_PUSHSTR: %s\n", v.s);
  return nextop + len;
}

uint8_t *op_add(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack.  If both ints, add them and push the
  // result onto the stack.  If both strings, concatenate them and do same.
  // If disparate types, push NIL onto the stack.
  (void)item;
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

uint8_t *op_subtract(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values, subtract the last from the first, then push the result
  // onto the stack. If either of the values is not an int, the result
  // is nil.
  (void)item;
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

uint8_t *op_divide(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values, divide the last by the first, then push the result
  // onto the stack. If either of the values is not an int, the result
  // is nil.
  // Trap divide by zero and substitute a result of zero.
  (void)item;
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

uint8_t *op_multiply(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values, multiply them together, then push the result onto the
  // stack.  If either of the values is not an int, the result is nil.
  (void)item;
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

uint8_t *op_negate(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the top value on the stack is an int, negate it.
  //  Complain bitterly if not.
  (void)item;
  if (!value_neg(&VM->stack->stack[VM->stack->current])) {
    logerr("Attempt to negate a value of type '%d'.\n",
                                 VM->stack->stack[VM->stack->current].type);
  }
  DISASS_LOG("OP_NEGATE: type %d\n", VM->stack->stack[VM->stack->current].type);
  return nextop;
}

uint8_t *op_equal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_EQ, "OP_EQUAL");
  return nextop;
}

uint8_t *op_notequal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_NE, "OP_NOTEQUAL");
  return nextop;
}

uint8_t *op_lessthan(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_LT, "OP_LESSTHAN");
  return nextop;
}

uint8_t *op_lessthanorequal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_LTE, "OP_LTEQ");
  return nextop;
}

uint8_t *op_greaterthan(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_GT, "OP_GREATERTHAN");
  return nextop;
}

uint8_t *op_greaterthanorequal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  pop_compare_and_push_bool(VM, CMP_GTE, "OP_GTEQ");
  return nextop;
}

uint8_t *op_logicalnot(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Logically negate the value on top of the stack.
  (void)item;
  VALUE_t *v = &VM->stack->stack[VM->stack->current];
  value_to_bool_inplace(v);
  v->i = !v->i;
  return nextop;
}

uint8_t *op_logicaland(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // AND the result and push it.
  (void)item;
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  value_to_bool_inplace(&v1);
  value_to_bool_inplace(&v2);
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i && v2.i; // Logical AND
  push_stack(VM->stack, v2);
  return nextop;
}

uint8_t *op_logicalor(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // OR the result and push it.
  (void)item;
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  value_to_bool_inplace(&v1);
  value_to_bool_inplace(&v2);
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i || v2.i; // Logical OR
  push_stack(VM->stack, v2);
  return nextop;
}

uint8_t *op_libcall_token(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  uint8_t token;
  nextop = decode_next(bc_read_u8(&ctx->decoder, nextop, &token, "OP_LIBCALL"));
  if (!nextop) return NULL;
  DISASS_LOG("Calling libcall token %d.\n", token);
  OP_t libcall = libcall_registry_func_token(&ctx->libcalls, token);
  if (!libcall) {
    char detail[64];
    snprintf(detail, sizeof(detail), "Unknown libcall token %u", token);
    logerr("%s.\n", detail);
    set_error_item(ERR_RUNTIME_INVLIB, detail);
    push_stack(VM->stack, VALUE_NIL);
  } else {
    nextop = libcall(ctx, nextop, item);
  }
  return nextop;
}


typedef struct {
  char *buf;
  uint32_t cap;
  uint32_t len;
} STRBUILDER_t;

static void sb_init(STRBUILDER_t *sb, uint32_t cap) {
  sb->buf = malloc(cap);
  sb->cap = cap;
  sb->len = 0;
  sb->buf[0] = '\0';
}

static void sb_ensure(STRBUILDER_t *sb, uint32_t add_len) {
  uint32_t need = sb->len + add_len + 1;
  if (need <= sb->cap) return;
  uint32_t new_cap = sb->cap;
  while (new_cap < need) new_cap *= 2;
  sb->buf = realloc(sb->buf, new_cap);
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

uint8_t *op_assigncodeitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Bytecode format assumption:
  //   ['P' <u16 len><bytes>...<u16 0>] optional parameter block,
  //   followed by mandatory <u16 source_len><source bytes>.
  CODEITEM_INPUT_t in = {0};
  VALUE_t itemname = VALUE_NIL;
  int8_t result = ERR_COMP_UNKNOWN;
  char *errdetail = NULL;

  if (*nextop == 'P') {
    nextop++;
    if (!decode_assigncode_params(ctx, &nextop, &in)) {
      set_error_item(ERR_COMP_UNKNOWN, "Invalid parameter block in code assignment bytecode.");
      goto cleanup;
    }
  }

  itemname = pop_stack(VM->stack);
  if (!decode_assigncode_source(ctx, &nextop, &in)) {
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
    free(itemname.s);
    itemname.s = strdup(fullname);
  }

  result = compile_and_insert_codeitem(ctx->itemroot, &itemname, &in, &errdetail);
  if (result == 0) {
    persist_codeitem_source(ctx->itemroot, &itemname, &in);
    set_item(ctx->itemroot, "error", VALUE_NIL);
    set_item(ctx->itemroot, "error.msg", VALUE_NIL);
  } else {
    logerr("Compilation failed.\n");
    set_error_item(result, errdetail);
  }

cleanup:
  if (errdetail) free(errdetail);
  if (in.source) free(in.source);
  if (in.params) {
    for (size_t pc = 0; pc < in.param_count; pc++) {
      free((char *)in.params[pc]);
    }
    free((char **)in.params);
  }
  if (in.param_lens) free(in.param_lens);
  FREE_STR(itemname);
  return nextop;
}

uint8_t *op_assignitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Save a value into an item.
  VALUE_t val = pop_stack(VM->stack); // value to be saved
  VALUE_t itemname = pop_stack(VM->stack); // Name of item to save into
  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (canonicalize_itemname(itemname.s, item, fullname)) {
      free(itemname.s);
      itemname.s = strdup(fullname);
    } else {
      logerr("Unable to create item '%s': failed to resolve canonical name.\n", itemname.s);
    }
  }
  assignitem(ctx->itemroot, &itemname, val);
  return nextop;
}

uint8_t *op_fetchitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Fetch a value from an item, and push it onto the stack.
  // The item name is a string at the top of the stack.
  // If the item is a code item, it is executed and the result pushed
  // onto the stack.
  // If the item does not exist, nil is pushed onto the stack.

  // First, let's get the number of arguments passed to this item
  uint16_t arg_count;
  nextop = decode_next(bc_read_u16(&ctx->decoder, nextop, &arg_count, "OP_FETCHITEM arg-count"));
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
    ITEM_t *i = find_item_cached(ctx->itemroot, fullname, NULL);
    if (i) {
      ITEMDEBUG_LOG("Fetched item %s (called with %d arguments).\n", fullname, arg_count);
      // Just push the item value onto the stack.
      if (i->type == ITEM_value) {
        VALUE_t v = value_clone(&i->value);
        push_stack(VM->stack, v);
      } else {
        if (!verify_runtime_bytecode(ctx, i)) {
          FREE_STR(itemname);
          return NULL;
        }
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
        push_callstack(VM, item, nextop, i->bytecode[1], (uint8_t *)ctx->decoder.frame_start, (uint8_t *)ctx->decoder.frame_end);
        // Invariant at call-entry:
        // - caller VM stack/base/locals/params are captured in callstack.
        // - caller continuation (item + nextop + bytecode bounds) is captured.
        // - interpreter loop must transfer control to callee without recursion.
        ITEMDEBUG_LOG("Executing item %s\n", i->name);
        ctx->pending_call_item = i;
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

uint8_t *assembleitem_helper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item, bool relative) {
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
            nextop = assembleitem_helper(ctx, nextop, item, deref_type == 'R');
            if (!nextop) {
              invalid = true;
              break;
            }
            VALUE_t layername = pop_stack(VM->stack);
            if (layername.type == VALUE_str) {
              //  This is basically the same as op_fetchitem
              ITEM_t *i = find_item(ctx->itemroot, layername.s);
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
    free(sb.buf);
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

uint8_t *op_assembleitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
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
  nextop = assembleitem_helper(ctx, nextop, item, false);
  return nextop;
}

uint8_t *op_assembleitem_rel(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  nextop = assembleitem_helper(ctx, nextop, item, true);
  return nextop;
}

uint8_t *op_delete(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // When this opcode is encountered, an item will previously have been
  // assembled and pushed onto the stack (or nil if the assembly failed).
  // Pop it, delete it, and return nothing.
  VALUE_t val = pop_stack(VM->stack);
  if (val.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (canonicalize_itemname(val.s, item, fullname)) {
      delete_item(ctx->itemroot, fullname);
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

uint8_t *op_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
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
  ITEM_t *i = find_item(ctx->itemroot, fullname);
  FREE_STR(val);
  push_stack(VM->stack, i ? VALUE_TRUE : VALUE_FALSE);
  DISASS_LOG("OP_EXISTS\n");
  return nextop;
}

uint8_t *op_nthname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // This is a very inefficient way to iterate over all the children
  // of an item.  Pop the index, then pop the item.  Find the indexed
  // child of the item and return its name as a string.  If the child
  // is not found, return nil.  There has to be a better way.
  (void)item;
  VALUE_t index = pop_stack(VM->stack);
  VALUE_t itemname = pop_stack(VM->stack);
  bool found = false;
  if (index.type == VALUE_int && index.i >= 0 && itemname.type == VALUE_str) {
    ITEM_t *i = find_item(ctx->itemroot, itemname.s);
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

uint8_t *op_rootname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Identical to op_nthname, except it only pops an index from the stack
  // and then uses it to index the itemroot.
  (void)item;
  VALUE_t index = pop_stack(VM->stack);
  bool found = false;
  if (index.type == VALUE_int && index.i >= 0) {
    ITEM_t *child = find_item_by_index(ctx->itemroot, index.i);
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

void init_interpreter(RuntimeContext *ctx) {
  if (ctx && !ctx->initialized) {
    (void)runtime_init(ctx, ctx->vm);
  }
}

VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item) {
  if (!ctx) return VALUE_NIL;
  RuntimeDecoder saved_decoder = ctx->decoder;
  ITEM_t *saved_current_item = ctx->current_item;
  ITEM_t *saved_pending_call_item = ctx->pending_call_item;
  if (!ctx->initialized) {
    init_interpreter(ctx);
  }
  set_item(ctx->itemroot, "error", VALUE_NIL);
  set_item(ctx->itemroot, "error.msg", VALUE_NIL);
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  ctx->current_item = item;
  ctx->pending_call_item = NULL;

  if (!verify_runtime_bytecode(ctx, ctx->current_item)) {
    ctx->current_item = saved_current_item;
    ctx->pending_call_item = saved_pending_call_item;
    ctx->decoder = saved_decoder;
    return VALUE_NIL;
  }

  // Enter initial frame.
  ctx->current_item->inuse = true;
  VM->stack->current += ctx->current_item->bytecode[0] - ctx->current_item->bytecode[1];
  VM->stack->locals = ctx->current_item->bytecode[0];
  VM->stack->params = ctx->current_item->bytecode[1];
  uint8_t *op = ctx->current_item->bytecode + 2;
  runtime_decoder_init(&ctx->decoder, op, ctx->current_item->bytecode + ctx->current_item->bytecode_len);

  while (true) {
    if (*op == 'h') {
      VALUE_t return_value = (size_stack(VM->stack) > 0) ? pop_stack(VM->stack) : VALUE_NIL;
      ctx->current_item->inuse = false;

      if (size_callstack(VM->callstack) == 0) {
        ctx->decoder = saved_decoder;
        ctx->current_item = saved_current_item;
        ctx->pending_call_item = saved_pending_call_item;
        return return_value;
      }

      FRAME_t *prev_frame = pop_callstack(VM);
      // Invariant at return:
      // - pop_callstack restored caller stack/base/locals/params.
      // - caller continuation state tells us exactly where to resume.
      ctx->current_item = prev_frame->item;
      ctx->current_item->inuse = true;
      op = prev_frame->nextop;
      runtime_decoder_init(&ctx->decoder, prev_frame->bytecode_start, prev_frame->bytecode_end);
      push_stack(VM->stack, return_value);
      continue;
    }

    uint8_t *nextop = op + 1;
    uint8_t *newop = ctx->opcode[*op](ctx, nextop, ctx->current_item);
    if (!newop) {
      if (ctx->pending_call_item) {
        ctx->current_item = ctx->pending_call_item;
        ctx->pending_call_item = NULL;
        ctx->current_item->inuse = true;
        VM->stack->current += ctx->current_item->bytecode[0] - ctx->current_item->bytecode[1];
        VM->stack->locals = ctx->current_item->bytecode[0];
        VM->stack->params = ctx->current_item->bytecode[1];
        op = ctx->current_item->bytecode + 2;
        runtime_decoder_init(&ctx->decoder, op, ctx->current_item->bytecode + ctx->current_item->bytecode_len);
        continue;
      }
      ctx->current_item->inuse = false;
      ctx->decoder = saved_decoder;
      ctx->current_item = saved_current_item;
      ctx->pending_call_item = saved_pending_call_item;
      return VALUE_NIL;
    }
    op = newop;
  }
}
