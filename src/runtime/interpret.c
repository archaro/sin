// The interpreter

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

#include "error.h"
#include "util.h"
#include "interpret.h"
#include "libcall.h"
#include "log.h"
#include "memory.h"
#include "parser.h"
#include "compiler/compiler_pipeline.h"
#include "bytecode_verify.h"
#include "value.h"
#include "stack.h"
#include "item.h"
#include "runtime_decode.h"
#include "runtime_item_ops.h"
#include "runtime_frame.h"
#include "runtime_opcode.h"
#include "runtime_value.h"
#include "itemref.h"
#include "list.h"
#include "strbuilder.h"

#define VM ctx->vm

static void runtime_verify_cache_clear(RuntimeContext *ctx);

static bool runtime_code_header(ITEM_t *item, BC_FormatHeader *header) {
  return item && bc_decode_header(item_bytecode(item), item_bytecode_length(item), header) == BC_FORMAT_OK;
}

typedef struct RuntimeRegistryNode {
  LibcallRegistry *registry;
  struct RuntimeRegistryNode *next;
} RuntimeRegistryNode;

static RuntimeRegistryNode *runtime_registry_nodes = NULL;
static bool runtime_registry_cleanup_registered = false;

static void runtime_registry_cleanup_all(void) {
  RuntimeRegistryNode *node = runtime_registry_nodes;
  runtime_registry_nodes = NULL;
  while (node) {
    RuntimeRegistryNode *next = node->next;
    libcall_registry_destroy(node->registry);
    free(node->registry);
    free(node);
    node = next;
  }
}

static bool runtime_registry_track(LibcallRegistry *registry) {
  RuntimeRegistryNode *node = malloc(sizeof(*node));
  if (!node) return false;
  node->registry = registry;
  node->next = runtime_registry_nodes;
  runtime_registry_nodes = node;
  if (!runtime_registry_cleanup_registered) {
    atexit(runtime_registry_cleanup_all);
    runtime_registry_cleanup_registered = true;
  }
  return true;
}

static void runtime_registry_untrack(LibcallRegistry *registry) {
  RuntimeRegistryNode **link = &runtime_registry_nodes;
  while (*link) {
    RuntimeRegistryNode *node = *link;
    if (node->registry == registry) {
      *link = node->next;
      free(node);
      return;
    }
    link = &node->next;
  }
}

bool runtime_init(RuntimeContext *ctx, VM_t *vm) {
  if (!ctx) return false;
  if (vm) ctx->vm = vm;
  if (ctx->initialized) return true;
  runtime_verify_cache_clear(ctx);
  if (!ctx->libcalls) {
    ctx->libcalls = calloc(1, sizeof(*ctx->libcalls));
    if (!ctx->libcalls) return false;
    if (!runtime_registry_track(ctx->libcalls)) {
      free(ctx->libcalls);
      ctx->libcalls = NULL;
      return false;
    }
  }
  if (!libcall_registry_init(ctx->libcalls)) {
    logerr("Failed to initialize libcall registry.\n");
    runtime_registry_untrack(ctx->libcalls);
    free(ctx->libcalls);
    ctx->libcalls = NULL;
    return false;
  }
  runtime_opcode_bind_table(ctx);
  ctx->initialized = true;
  return true;
}

void runtime_destroy(RuntimeContext *ctx) {
  if (!ctx) return;
  runtime_verify_cache_clear(ctx);
  if (ctx->libcalls) {
    runtime_registry_untrack(ctx->libcalls);
    libcall_registry_destroy(ctx->libcalls);
    free(ctx->libcalls);
    ctx->libcalls = NULL;
  }
  memset(ctx->opcode, 0, sizeof(ctx->opcode));
  ctx->initialized = false;
}

void runtime_context_init(RuntimeContext *ctx, VM_t *vm) {
  if (!ctx) return;
  memset(ctx, 0, sizeof(*ctx));
  ctx->vm = vm;
}

static const char *runtime_item_label(ITEM_t *item, char *buffer, size_t size) {
  if (!item) return "<null>";
  if (item_parent(item) && item_parent(item_parent(item)) && size >= MAX_ITEM_NAME) {
    buffer[0] = '\0';
    get_itemname(item, buffer);
    return buffer;
  }
  return item_layer_name(item);
}

static void set_runtime_bytecode_error(RuntimeContext *ctx, const char *label,
                                       uint32_t offset, const char *message) {
  char item_name[MAX_ITEM_NAME] = {0};
  const char *safe_label = label ? label
      : runtime_item_label(ctx ? ctx->current_item : NULL,
                           item_name, sizeof(item_name));
  const char *safe_message = message ? message : "<no diagnostic>";
  int needed = snprintf(NULL, 0,
      "Runtime bytecode validation failed for item '%s' at offset %u: %s",
      safe_label, offset, safe_message);
  if (needed < 0) {
    logerr("Runtime bytecode validation failed.\n");
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_BYTECODE,
                       "Runtime bytecode validation failed.",
                       ctx ? ctx->current_item : NULL);
    return;
  }

  size_t detail_len = (size_t)needed + 1u;
  char *detail = alloc_malloc(detail_len);
  if (!detail) {
    logerr("Runtime bytecode validation failed: out of memory while formatting diagnostic.\n");
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_BYTECODE,
                       "Runtime bytecode validation failed: out of memory while formatting diagnostic.",
                       ctx ? ctx->current_item : NULL);
    return;
  }

  snprintf(detail, detail_len,
      "Runtime bytecode validation failed for item '%s' at offset %u: %s",
      safe_label, offset, safe_message);
  logerr("%s.\n", detail);
  set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_BYTECODE,
                         detail, ctx ? ctx->current_item : NULL);
  free(detail);
}


static void report_strict_runtime_contract(RuntimeContext *ctx, const char *detail) {
  if (!ctx || !ctx->strict_runtime_contracts) return;
  logerr("Runtime contract violation: %s.\n", detail ? detail : "<no detail>");
  set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_INVALIDARGS, detail,
                         ctx->current_item);
}

static void runtime_verify_cache_clear(RuntimeContext *ctx) {
  if (!ctx) return;
  memset(ctx->runtime_verify_cache, 0, sizeof(ctx->runtime_verify_cache));
  ctx->runtime_verify_cache_next = 0u;
  ctx->runtime_verify_cache_owner = NULL;
  ctx->runtime_verify_cache_topology_revision = 0u;
  ctx->runtime_verify_cache_topology_epoch = 0u;
  ctx->runtime_verify_cache_revision = 0u;
  ctx->runtime_verify_cache_revision_epoch = 0u;
  ctx->runtime_verify_cache_revision_valid = false;
}

static void runtime_verify_cache_sync(RuntimeContext *ctx,
                                      ITEMSTORE_t *owner,
                                      uint64_t topology_revision,
                                      uint64_t topology_epoch,
                                      uint64_t revision_epoch,
                                      uint64_t revision) {
  if (!ctx) return;
  if (!ctx->runtime_verify_cache_revision_valid ||
      ctx->runtime_verify_cache_owner != owner ||
      ctx->runtime_verify_cache_topology_revision != topology_revision ||
      ctx->runtime_verify_cache_topology_epoch != topology_epoch ||
      ctx->runtime_verify_cache_revision_epoch != revision_epoch ||
      ctx->runtime_verify_cache_revision != revision) {
    runtime_verify_cache_clear(ctx);
    ctx->runtime_verify_cache_owner = owner;
    ctx->runtime_verify_cache_topology_revision = topology_revision;
    ctx->runtime_verify_cache_topology_epoch = topology_epoch;
    ctx->runtime_verify_cache_revision_epoch = revision_epoch;
    ctx->runtime_verify_cache_revision = revision;
    ctx->runtime_verify_cache_revision_valid = true;
  }
}

static bool runtime_verify_cache_matches(const RuntimeVerifyCacheEntry *entry,
                                         const uint8_t *bytecode,
                                         uint32_t bytecode_length,
                                         ITEMSTORE_t *owner,
                                         uint64_t topology_epoch,
                                         uint64_t revision_epoch,
                                         uint64_t revision) {
  return entry && entry->valid && entry->bytecode == bytecode &&
      entry->bytecode_length == bytecode_length && entry->owner == owner &&
      entry->topology_revision_epoch == topology_epoch &&
      entry->payload_revision_epoch == revision_epoch &&
      entry->payload_revision == revision &&
      entry->policy_id == RUNTIME_VERIFY_POLICY_ID;
}

static void runtime_verify_cache_insert(RuntimeContext *ctx,
                                        const uint8_t *bytecode,
                                        uint32_t bytecode_length,
                                        ITEMSTORE_t *owner,
                                        uint64_t topology_epoch,
                                        uint64_t revision_epoch,
                                        uint64_t revision) {
  if (!ctx || !owner) return;
  RuntimeVerifyCacheEntry *entry =
      &ctx->runtime_verify_cache[ctx->runtime_verify_cache_next];
  *entry = (RuntimeVerifyCacheEntry){
      .valid = true,
      .bytecode = bytecode,
      .bytecode_length = bytecode_length,
      .owner = owner,
      .topology_revision_epoch = topology_epoch,
      .payload_revision_epoch = revision_epoch,
      .payload_revision = revision,
      .policy_id = RUNTIME_VERIFY_POLICY_ID,
  };
  ctx->runtime_verify_cache_next =
      (ctx->runtime_verify_cache_next + 1u) % RUNTIME_VERIFY_CACHE_SIZE;
}

static bool verify_runtime_bytecode(RuntimeContext *ctx, ITEM_t *item) {
  char item_name[MAX_ITEM_NAME] = {0};
  const char *label = runtime_item_label(item, item_name, sizeof(item_name));
  if (!item || item_kind(item) != ITEM_code) {
    set_runtime_bytecode_error(ctx, label, 0, "item is not executable code");
    return false;
  }
  if (!item_bytecode(item)) {
    set_runtime_bytecode_error(ctx, label, 0, "null bytecode pointer");
    return false;
  }
  ITEMSTORE_t *owner = itemstore_owner(item);
  uint64_t topology_revision = owner ? itemstore_topology_revision(owner) : 0u;
  uint64_t topology_epoch = owner ? itemstore_topology_revision_epoch(owner) : 0u;
  uint64_t revision_epoch = owner ? itemstore_payload_revision_epoch(owner) : 0u;
  uint64_t revision = owner ? itemstore_payload_revision(owner) : 0u;
  bool token_exhausted = owner &&
      (itemstore_payload_revision_token_exhausted(owner) ||
       itemstore_topology_revision_token_exhausted(owner));
  if (owner && ctx) {
    if (token_exhausted) {
      runtime_verify_cache_clear(ctx);
    } else {
      runtime_verify_cache_sync(ctx, owner, topology_revision, topology_epoch,
                                revision_epoch, revision);
      for (size_t i = 0u; i < RUNTIME_VERIFY_CACHE_SIZE; i++) {
        if (runtime_verify_cache_matches(&ctx->runtime_verify_cache[i],
                                         item_bytecode(item),
                                         item_bytecode_length(item), owner,
                                         topology_epoch,
                                         revision_epoch,
                                         revision)) {
          return true;
        }
      }
    }
  }
  if (ctx && ctx->verifier_invocations_for_tests != UINT64_MAX) {
    ctx->verifier_invocations_for_tests++;
  }
  BC_VerifyResult result = bc_verify_executable_bytecode(
      item_bytecode(item), item_bytecode_length(item), label);
  if (result.status != BC_VERIFY_ERROR) {
    if (owner && !token_exhausted) {
      runtime_verify_cache_insert(ctx, item_bytecode(item),
                                  item_bytecode_length(item), owner,
                                  topology_epoch,
                                  revision_epoch, revision);
    }
    return true;
  }

  set_runtime_bytecode_error(ctx, label, result.diagnostic.offset,
                             result.diagnostic.message);
  return false;
}

uint64_t runtime_verify_invocations_for_tests(const RuntimeContext *ctx) {
  return ctx ? ctx->verifier_invocations_for_tests : 0u;
}

void runtime_verify_cache_clear_for_tests(RuntimeContext *ctx) {
  runtime_verify_cache_clear(ctx);
}

void runtime_verify_invocations_reset_for_tests(RuntimeContext *ctx) {
  if (ctx) ctx->verifier_invocations_for_tests = 0u;
}

static bool consume_runtime_interrupt(RuntimeContext *ctx) {
  if (!ctx || !ctx->interrupt_pending || !*ctx->interrupt_pending) return false;
  *ctx->interrupt_pending = 0;
  ctx->interrupted = true;
  return true;
}

static bool report_decode_status(RuntimeContext *ctx, RuntimeDecodeStatus status) {
  if (runtime_decode_status_ok(status)) return true;
  if (status.code == RUNTIME_DECODE_TRUNCATED) {
    logerr("%s.\n", status.detail);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_TRUNCATED,
                           status.detail, ctx ? ctx->current_item : NULL);
  }
  return false;
}

static uint8_t *decode_next(RuntimeContext *ctx, RuntimeDecodeStatus status) {
  return report_decode_status(ctx, status) ? status.next : NULL;
}

static uint32_t runtime_absolute_offset(const RuntimeContext *ctx,
                                       const uint8_t *pointer) {
  const uint8_t *base = ctx && ctx->current_item
      ? item_bytecode(ctx->current_item) : NULL;
  uint32_t length = ctx && ctx->current_item
      ? item_bytecode_length(ctx->current_item) : 0u;
  if (!base || !pointer || pointer < base || pointer > base + length) return 0u;
  return (uint32_t)(pointer - base);
}

static uint8_t *runtime_jump_target(RuntimeContext *ctx, uint8_t *origin,
                                    int16_t relative, const char *opname) {
  if (!ctx || !ctx->decoder.frame_start || !ctx->decoder.frame_end ||
      origin < ctx->decoder.frame_start || origin > ctx->decoder.frame_end) {
    set_runtime_bytecode_error(ctx, NULL, 0, "invalid jump decoder bounds");
    return NULL;
  }
  ptrdiff_t origin_offset = origin - ctx->decoder.frame_start;
  ptrdiff_t frame_len = ctx->decoder.frame_end - ctx->decoder.frame_start;
  int64_t target_offset = (int64_t)origin_offset + relative;
  if (target_offset < 0 || target_offset >= frame_len) {
    char detail[128];
    snprintf(detail, sizeof(detail), "%s target is outside the bytecode frame",
             opname);
    uint32_t bytecode_offset = runtime_absolute_offset(ctx, origin);
    set_runtime_bytecode_error(ctx, NULL, bytecode_offset, detail);
    return NULL;
  }
  return (uint8_t *)ctx->decoder.frame_start + (size_t)target_offset;
}

#define REQUIRE_BYTES(nextop, n, opname) \
  do { if (!report_decode_status(ctx, require_bytes(&ctx->decoder, (nextop), (n), (opname)))) return NULL; } while (0)

static void log_invalid_binary_operands(const char *opcode_name,
                                        const VALUE_t *left,
                                        const VALUE_t *right) {
  logverbose("%s invalid operand types: left '%s', right '%s'.\n",
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

  value_free(&v1);
  value_free(&v2);
  push_stack(vm->stack, result);
  (void)opcode_tag;
  if (!result.i) {
    logverbose("%s: types %d and %d\n", opcode_tag, v1_type, v2_type);
  }
  return result.i != 0;
}


uint8_t *op_nop(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)ctx;
  (void)item;
  return nextop;
}

uint8_t *op_undefined(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  uint8_t opcode = *(nextop - 1);
  uint32_t offset = 0;
  if (ctx && ctx->current_item && item_bytecode(ctx->current_item)) {
    offset = (uint32_t)((nextop - 1) - item_bytecode(ctx->current_item));
  }
  char detail[144];
  snprintf(detail, sizeof(detail),
           "invalid opcode 0x%02X at byte offset %u; recompile from Sinistra source",
           opcode, offset);
  set_runtime_bytecode_error(ctx, NULL, offset, detail);
  return NULL;
}

uint8_t *op_pushint(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  (void)item;
  VALUE_t v;
  v.type = VALUE_int;
  nextop = decode_next(ctx, bc_read_i64(&ctx->decoder, nextop, &v.i, "OP_PUSHINT"));
  if (!nextop) return NULL;
  push_stack(VM->stack, v);
  logverbose("OP_PUSHINT: %ld\n", v.i);
  return nextop;
}

uint8_t *op_pushfloat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t v;
  v.type = VALUE_float;
  uint64_t bits;
  nextop = decode_next(ctx, bc_read_u64_payload(&ctx->decoder, nextop, &bits, "OP_PUSHFLOAT"));
  if (!nextop) return NULL;
  v.f = value_float_from_bits(bits);
  push_stack(VM->stack, v);
  logverbose("OP_PUSHFLOAT: %g (0x%016llx)\n", v.f, (unsigned long long)bits);
  return nextop;
}

uint8_t *op_pushbool(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  uint8_t raw;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &raw, "OP_PUSHBOOL"));
  if (!nextop) return NULL;
  VALUE_t v;
  v.type = VALUE_bool;
  v.i = (raw != 0) ? 1 : 0;
  push_stack(VM->stack, v);
  logverbose("OP_PUSHBOOL: %ld\n", v.i);
  return nextop;
}

uint8_t *op_pushnil(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  push_stack(ctx->vm->stack, VALUE_NIL);
  logverbose("OP_PUSHNIL\n");
  return nextop;
}

uint8_t *op_inclocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, increment it.  Otherwise complain.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_INCLOCAL"));
  if (!nextop) return NULL;
  int32_t index = (int32_t)raw_index + VM->stack->base;
  if (index < 0 || index >= STACK_SIZE) return NULL;
  if (VM->stack->stack[index].type == VALUE_int) {
    VALUE_t one = {VALUE_int, {.i = 1}};
    VALUE_t result = VALUE_NIL;
    (void)value_add(&VM->stack->stack[index], &one, &result);
    value_replace(&VM->stack->stack[index], result);
  } else {
    logverbose("Trying to increment non integer local variable.\n");
  }
  logverbose("OP_INCLOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_declocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, decrement it.  Otherwise complain.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_DECLOCAL"));
  if (!nextop) return NULL;
  int32_t index = (int32_t)raw_index + VM->stack->base;
  if (index < 0 || index >= STACK_SIZE) return NULL;
  if (VM->stack->stack[index].type == VALUE_int) {
    VALUE_t one = {VALUE_int, {.i = 1}};
    VALUE_t result = VALUE_NIL;
    (void)value_sub(&VM->stack->stack[index], &one, &result);
    value_replace(&VM->stack->stack[index], result);
  } else {
    logverbose("Trying to decrement non integer local variable.\n");
  }
  logverbose("OP_DECLOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_jump(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Unconditional jump.  Interpret the next two bytes as a
  // SIGNED int, and then modify the bytecode pointer by that amount.
  (void)item;
  int16_t offset;
  nextop = decode_next(ctx, bc_read_i16(&ctx->decoder, nextop, &offset, "OP_JUMP"));
  if (!nextop) return NULL;
  logverbose("OP_JUMP: offset is  %d.\n", offset);
  return runtime_jump_target(ctx, nextop - sizeof(offset), offset, "OP_JUMP");
}

uint8_t *op_jumpfalse(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Evaluate the top of the stack.  If false, interpret next
  // two bytes as a SIGNED int, and modify the bytecode pointer
  // by that amount.  Alternatively, if true, simply skip the next
  // two bytes and go on to the next instruction.

  (void)item;
  int16_t offset;
  uint8_t *offset_start = nextop;
  nextop = decode_next(ctx, bc_read_i16(&ctx->decoder, nextop, &offset, "OP_JUMPFALSE"));
  if (!nextop) return NULL;
  VALUE_t v1;
  v1 = pop_stack(VM->stack);
  if (value_is_truthy(&v1)) {
    // A true value means that we don't branch.  Skip over
    // the next two bytes.
    logverbose("OP_JUMPFALSE: evaluates to true (no jump).\n");
    value_free(&v1);
    return nextop;
  } else {
    // If not true then it must be false.  That's logic.
    logverbose("OP_JUMPFALSE: evaluates to false (jump offset %d).\n", offset);
    value_free(&v1);
    return runtime_jump_target(ctx, offset_start, offset, "OP_JUMPFALSE");
  }
}

uint8_t *op_savelocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_SAVELOCAL"));
  if (!nextop) return NULL;
  int32_t index = (int32_t)raw_index + VM->stack->base;
  if (index < 0 || index >= STACK_SIZE) return NULL;
  // First check if the current value is a string.  If so, free it.
  VALUE_t top = pop_stack(VM->stack);
  value_move(&VM->stack->stack[index], &top);
  logverbose("OP_SAVELOCAL: index %d\n", index);
  return nextop;
}

uint8_t *op_getlocal(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  (void)item;
  uint8_t raw_index;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &raw_index, "OP_GETLOCAL"));
  if (!nextop) return NULL;
  int32_t index = (int32_t)raw_index + VM->stack->base;
  if (index < 0 || index >= STACK_SIZE) return NULL;

  push_stack(VM->stack, value_clone(&VM->stack->stack[index]));
  VALUE_t *v = peek_stack(VM->stack);
  if (!v) return NULL;
  switch (v->type) {
    case VALUE_int:
      logverbose("OP_GETLOCAL: index %d value %" PRId64 ".\n", index, v->i);
      break;
    case VALUE_str:
      logverbose("OP_GETLOCAL: index %d value '%s'.\n", index, v->s);
      break;
    default:
      logverbose("OP_GETLOCAL: index %d type %d.\n", index, v->type);
  }
  return nextop;
}

uint8_t *op_pushstr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Push a string literal onto the stack.
  (void)item;
  VALUE_t v;
  v.type = VALUE_str;
  uint16_t len;
  // Get the length
  nextop = decode_next(ctx, bc_read_u16(&ctx->decoder, nextop, &len, "OP_PUSHSTR length"));
  if (!nextop) return NULL;
  REQUIRE_BYTES(nextop, len, "OP_PUSHSTR payload");
  v.s = alloc_malloc((size_t)len + 1u);
  if (!v.s) {
    logerr("Unable to allocate string literal.\n");
    return NULL;
  }
  memcpy(v.s, nextop, len);
  v.s[len] = 0;
  push_stack(VM->stack, v);
  logverbose("OP_PUSHSTR: %s\n", v.s);
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
    VALUE_e v1_type = v1.type;
    VALUE_e v2_type = v2.type;
    value_free(&v1);
    value_free(&v2);
    logverbose("OP_ADD: operand types %d and %d\n", v2_type, v1_type);
    push_stack(VM->stack, result);
  } else if (v1.type == VALUE_str && v2.type == VALUE_str) {
    push_stack(VM->stack, concat_two_strings(v2, v1));
  } else {
    VALUE_e left_type = v2.type;
    VALUE_e right_type = v1.type;
    value_free(&v1);
    value_free(&v2);
    logverbose("OP_ADD invalid operand types: left '%s', right '%s'. Result is NIL.\n",
               value_type_name(left_type), value_type_name(right_type));
    push_stack(VM->stack, VALUE_NIL);
  }
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
    logverbose("OP_SUB: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_SUB", &v2, &v1);
    logverbose("OP_SUB: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free(&v1);
  value_free(&v2);
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
      logverbose("Attempt to divide by zero.  Substitute zero as result.\n");
    }
    logverbose("OP_DIV: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_DIV", &v2, &v1);
    logverbose("OP_DIV: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free(&v1);
  value_free(&v2);
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
    logverbose("OP_MUL: operand types %d and %d\n", v2.type, v1.type);
  } else {
    log_invalid_binary_operands("OP_MUL", &v2, &v1);
    logverbose("OP_MUL: invalid operand types %d and %d\n", v2.type, v1.type);
  }
  value_free(&v1);
  value_free(&v2);
  push_stack(VM->stack, result);
  return nextop;
}

uint8_t *op_modulo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t v1 = pop_stack(VM->stack);
  VALUE_t v2 = pop_stack(VM->stack);
  VALUE_t result;
  (void)value_mod(&v2, &v1, &result);
  value_free(&v1);
  value_free(&v2);
  push_stack(VM->stack, result);
  return nextop;
}

uint8_t *op_negate(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the top value on the stack is an int, negate it.
  //  Complain bitterly if not.
  (void)item;
  if (!value_neg(&VM->stack->stack[VM->stack->current])) {
    logverbose("Attempt to negate a value of type '%d'.\n",
               VM->stack->stack[VM->stack->current].type);
  }
  logverbose("OP_NEGATE: type %d\n", VM->stack->stack[VM->stack->current].type);
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

uint8_t *op_libcall(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  uint8_t lib_index, call_index;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &lib_index, "OP_LIBCALL"));
  if (!nextop) return NULL;
  nextop = decode_next(ctx, bc_read_u8(&ctx->decoder, nextop, &call_index, "OP_LIBCALL"));
  if (!nextop) return NULL;
  logverbose("Calling libcall pair (%u,%u).\n", lib_index, call_index);
  OP_t libcall = libcall_registry_func_pair(ctx->libcalls, lib_index, call_index);
  if (!libcall) {
    char detail[96];
    snprintf(detail, sizeof(detail), "Unknown libcall pair (%u,%u)", lib_index, call_index);
    logerr("%s.\n", detail);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INVLIB,
                           detail, ctx ? ctx->current_item : NULL);
    push_stack(VM->stack, VALUE_NIL);
  } else {
    nextop = libcall(ctx, nextop, item);
  }
  return nextop;
}


static bool sb_append_intstr(SIN_STRBUILDER_t *sb, int64_t val) {
  char str[22];
  snprintf(str, sizeof(str), "%" PRId64, val);
  return sin_sb_append_cstr(sb, str);
}

uint8_t *op_assigncodeitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Bytecode format assumption:
  //   'P' <u16 len><bytes>...<u16 0> mandatory parameter block,
  //   followed by mandatory <u16 source_len><source bytes>.
  CODEITEM_INPUT_t in = {0};
  VALUE_t itemname = VALUE_NIL;
  int8_t result = ERR_COMP_UNKNOWN;
  char *errdetail = NULL;

  RuntimeDecodeStatus marker_status = require_bytes(
      ctx ? &ctx->decoder : NULL, nextop, 1, "OP_ASSIGNCODEITEM");
  if (!runtime_decode_status_ok(marker_status) || *nextop != 'P') {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL,
                   ERR_RUNTIME_BYTECODE,
                   "Missing parameter marker in code assignment bytecode.",
                   ctx ? ctx->current_item : NULL);
    nextop = NULL;
    goto cleanup;
  }
  nextop++;
  if (!decode_assigncode_params(ctx, &nextop, &in)) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL,
                   ERR_RUNTIME_BYTECODE,
                   "Invalid parameter block in code assignment bytecode.",
                   ctx ? ctx->current_item : NULL);
    nextop = NULL;
    goto cleanup;
  }

  itemname = pop_stack(VM->stack);
  if (!decode_assigncode_source(ctx, &nextop, &in)) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_BYTECODE,
                           "Invalid source block in code assignment bytecode.",
                           ctx ? ctx->current_item : NULL);
    nextop = NULL;
    goto cleanup;
  }

  logverbose("Source to compile: %s\n", in.source);
  if (itemname.type != VALUE_str) {
    logerr("Unable to assign code item: invalid name type %d.\n", itemname.type);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INVALIDITEM,
                           "Invalid item name type for code assignment.",
                           ctx ? ctx->current_item : NULL);
    goto cleanup;
  }

  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (!canonicalize_itemname(itemname.s, item, fullname)) {
      set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INVALIDITEM,
                             "Invalid item name for code assignment.",
                             ctx ? ctx->current_item : NULL);
      goto cleanup;
    }
    free_runtime_string(itemname.s);
    itemname.s = strdup(fullname);
  }

  result = compile_and_insert_codeitem(itemstore_root(ctx->itemstore), &itemname, &in, &errdetail);
  if (result == 0) {
    persist_codeitem_source(itemstore_root(ctx->itemstore), &itemname, &in, ctx ? ctx->srcroot : NULL);
    clear_error_item(itemstore_root(ctx->itemstore));
  } else {
    logerr("Compilation failed.\n");
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, result, errdetail,
                           ctx ? ctx->current_item : NULL);
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
      free_runtime_string(itemname.s);
      itemname.s = strdup(fullname);
    } else {
      logverbose("Unable to create item '%s': failed to resolve canonical name.\n", itemname.s);
    }
  }
  assignitem(itemstore_root(ctx->itemstore), &itemname, val);
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
  nextop = decode_next(ctx, bc_read_u16(&ctx->decoder, nextop, &arg_count, "OP_FETCHITEM arg-count"));
  if (!nextop) return NULL;

  // Now the item name.
  VALUE_t itemname = pop_stack(VM->stack);

  // First check to see if there is a valid item to look up
  if (itemname.type == VALUE_str) {
    char fullname[MAX_ITEM_NAME];
    if (!canonicalize_itemname(itemname.s, item, fullname)) {
      logverbose("Unable to fetch item '%s': failed to resolve canonical name.\n", itemname.s);
      while (arg_count > 0) {
        logverbose("Discarding argument for invalid canonical fetch name.\n");
        report_strict_runtime_contract(ctx, "OP_FETCHITEM discarded argument for invalid canonical fetch name");
        throwaway_stack(VM->stack);
        arg_count--;
      }
      push_stack(VM->stack, VALUE_NIL);
      FREE_STR(itemname);
      return nextop;
    }
    ITEM_t *i = find_item_cached(itemstore_root(ctx->itemstore), fullname, NULL);
    if (i) {
      logverbose("Fetched item %s (called with %d arguments).\n", fullname, arg_count);
      // Just push the item value onto the stack.
      if (item_kind(i) == ITEM_value) {
        while (arg_count > 0) {
          logverbose("Discarding argument for value target item.\n");
          report_strict_runtime_contract(ctx,
              "OP_FETCHITEM discarded argument for value target item");
          throwaway_stack(VM->stack);
          arg_count--;
        }
        const VALUE_t *iv = item_value(i);
        VALUE_t v = iv ? value_clone(iv) : VALUE_NIL;
        push_stack(VM->stack, v);
      } else {
        if (!verify_runtime_bytecode(ctx, i)) {
          FREE_STR(itemname);
          return NULL;
        }
        BC_FormatHeader iheader;
        if (!runtime_code_header(i, &iheader)) { FREE_STR(itemname); return NULL; }
        size_t discarded_args = 0u;
        if (!runtime_frame_prepare_call(ctx, item, nextop, i, arg_count,
                                        iheader.locals, iheader.params,
                                        (uint8_t *)ctx->decoder.frame_start,
                                        (uint8_t *)ctx->decoder.frame_end,
                                        &discarded_args)) {
          set_runtime_bytecode_error(ctx, NULL, 0,
              "unable to enter call frame: VM capacity exhausted");
          FREE_STR(itemname);
          return NULL;
        }
        for (size_t discarded = 0u; discarded < discarded_args; discarded++) {
          logverbose("Popping unneeded argument.\n");
          report_strict_runtime_contract(ctx,
              "OP_FETCHITEM discarded extra argument for target item");
        }
        // Invariant at call-entry:
        // - caller VM stack/base/locals/params are captured in callstack.
        // - caller continuation (item + nextop + bytecode bounds) is captured.
        // - interpreter loop must transfer control to callee without recursion.
        logverbose("Executing item %s\n", item_layer_name(i));
        FREE_STR(itemname);
        return NULL;
      }
    } else {
      // Item not found.
      logverbose("Item '%s' not found.\n", fullname);
      // We need to lose any values on the stack which were passed as args.
        while (arg_count > 0) {
          logverbose("Popping unneeded argument.\n");
          report_strict_runtime_contract(ctx, "OP_FETCHITEM discarded argument for missing target item");
          throwaway_stack(VM->stack);
          arg_count--;
        }
      push_stack(VM->stack, VALUE_NIL);
    }
    FREE_STR(itemname);
  } else {
    logverbose("Unable to fetch item: invalid item type for name: %d.\n", itemname.type);
    while (arg_count > 0) {
      logverbose("Discarding argument for invalid item fetch name type.\n");
      report_strict_runtime_contract(ctx, "OP_FETCHITEM discarded argument for invalid item fetch name type");
      throwaway_stack(VM->stack);
      arg_count--;
    }
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *op_discard(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  throwaway_stack(ctx->vm->stack);
  return nextop;
}

uint8_t *op_build_list(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  uint32_t count = 0;
  VALUE_t *values = NULL;
  (void)item;
  RuntimeDecodeStatus status = bc_read_u32(&ctx->decoder, nextop, &count, "OP_BUILD_LIST count");
  if (!runtime_decode_status_ok(status)) return NULL;
  nextop = status.next;
  if (count > SIN_LIST_MAX_ELEMENTS) {
    set_runtime_bytecode_error(
        ctx, NULL, runtime_absolute_offset(ctx, nextop - 4u),
                               "BUILD_LIST count exceeds maximum");
    return NULL;
  }
  if ((size_t)count > (size_t)size_stack(VM->stack)) {
    set_runtime_bytecode_error(
        ctx, NULL, runtime_absolute_offset(ctx, nextop - 5u),
                               "BUILD_LIST stack underflow");
    return NULL;
  }
  if (count == 0) {
    SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
    if (!empty) {
      push_stack(VM->stack, VALUE_NIL);
      return nextop;
    }
    push_stack(VM->stack, (VALUE_t){VALUE_list, {.list = empty}});
    return nextop;
  }
  values = alloc_calloc(count, sizeof(*values));
  if (!values) {
    while (count--) throwaway_stack(VM->stack);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  for (uint32_t i = count; i > 0; i--) {
    values[i - 1] = pop_stack(VM->stack);
  }
  SIN_LIST_t *list = sin_list_build_owned(values, count);
  free(values);
  if (!list) {
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  push_stack(VM->stack, (VALUE_t){VALUE_list, {.list = list}});
  return nextop;
}

uint8_t *op_make_itemref(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  if (size_stack(VM->stack) < 1) {
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  VALUE_t assembled = pop_stack(VM->stack);
  if (assembled.type != VALUE_str) {
    value_free(&assembled);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  char fullname[MAX_ITEM_NAME];
  if (!canonicalize_itemname(assembled.s, item, fullname)) {
    value_free(&assembled);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  SIN_ITEMREF_t *ref = sin_itemref_create(fullname);
  value_free(&assembled);
  if (!ref) {
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  push_stack(VM->stack, (VALUE_t){VALUE_itemref, {.itemref = ref}});
  return nextop;
}

typedef struct {
  bool saw_missing_layer;
  bool saw_non_missing_layer;
  bool missing_layer_is_leading;
  bool missing_layer_possibly_leading;
} ItemAssemblyState;

static bool append_layer_from_value(SIN_STRBUILDER_t *sb, const VALUE_t *value,
                                    ItemAssemblyState *state,
                                    const char *item_deref_name) {
  switch (value->type) {
    case VALUE_str: {
      if (value->s[0] == '\0') {
        if (!state->saw_non_missing_layer && !state->saw_missing_layer) {
          state->saw_missing_layer = true;
          state->missing_layer_possibly_leading = true;
        } else {
          logverbose("Missing layer name in non-leading position.\n");
          return false;
        }
      } else if (is_valid_layer(value->s)) {
        if (!sin_sb_append_cstr(sb, value->s)) return false;
        state->saw_non_missing_layer = true;
      } else {
        logverbose("Invalid layer name '%s'.\n", value->s);
        return false;
      }
      return true;
    }
    case VALUE_int: {
      if (!sb_append_intstr(sb, value->i)) return false;
      state->saw_non_missing_layer = true;
      return true;
    }
    case VALUE_float: {
      if (item_deref_name) {
        logverbose("Item dereference failed for '%s': float value cannot be used as an item layer name.\n",
                   item_deref_name);
      } else {
        logverbose("Float value cannot be used as an item layer name.\n");
      }
      return false;
    }
    case VALUE_nil: {
      if (!state->saw_non_missing_layer && !state->saw_missing_layer) {
        state->saw_missing_layer = true;
        state->missing_layer_possibly_leading = true;
      } else {
        logverbose("Missing layer name in non-leading position.\n");
        return false;
      }
      return true;
    }
    default: {
      if (item_deref_name) {
        logverbose("Item dereference failed for '%s': invalid type.\n", item_deref_name);
      } else {
        logverbose("Layer type (%d) not int or string.\n", value->type);
      }
      return false;
    }
  }
}

uint8_t *assembleitem_helper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item, bool relative) {
  // Interpret the following bytecode as an item.  If an item can be
  // assembled, push the full item name onto the stack as a string.
  // Return a pointer to the bytecode after the item assembly.
  // May recurse - necessary for the handling of nested derefs.
  bool invalid = false;
  ItemAssemblyState layer_state = {0};
  bool just_processed_layer = false;
  SIN_STRBUILDER_t sb;
  if (!sin_sb_init(&sb, 130u, MAX_ITEM_NAME - 1u)) {
    push_stack(VM->stack, VALUE_NIL);
    return NULL;
  }
  if (relative) {
    if (!item) {
      logverbose("Relative item assembly requires current item context.\n");
      invalid = true;
    } else {
      char parent[MAX_ITEM_NAME];
      get_itemname(item, parent);
      if (!sin_sb_append_cstr(&sb, parent) || !sin_sb_append_cstr(&sb, ".")) invalid = true;
    }
  }

  while (!invalid) {
    REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM layer");
    if (*nextop == 'E') {
      break;
    }
    uint8_t layer_type = *nextop++;
    switch (layer_type) {
      case 'L': {
        // Simple layer
        REQUIRE_BYTES(nextop, 1, "OP_ASSEMBLEITEM layer length");
        int s = *nextop++; // Length of layer name
        REQUIRE_BYTES(nextop, (size_t)s, "OP_ASSEMBLEITEM layer bytes");
        if (!sin_sb_append_bytes(&sb, (char *)nextop, (size_t)s)) invalid = true;
        nextop += s;
        layer_state.saw_non_missing_layer = true;
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
            invalid = !append_layer_from_value(&sb, &VM->stack->stack[idx], &layer_state, NULL);
            just_processed_layer = !invalid;
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
              ITEM_t *i = find_item(itemstore_root(ctx->itemstore), layername.s);
              if (i && item_kind(i) == ITEM_value) {
                // We have an item.  Convert its value into the dereferenced layer.
                const VALUE_t *iv = item_value(i);
                invalid = !iv || !append_layer_from_value(&sb, iv, &layer_state, layername.s);
                just_processed_layer = !invalid;
              } else if (i) {
                logverbose("Item dereference failed for '%s': target is a code item.\n",
                           layername.s);
                invalid = true;
              } else {
                logverbose("Item dereference failed for '%s'.\n", layername.s);
                invalid = true;
              }
              FREE_STR(layername);
            } else {
              logverbose("Invalid item layer type %d.\n", layername.type);
              invalid = true;
            }
            break;
          }
          default: {
            logerr("Invalid dereference layer type '%c' (%d).\n", deref_type, deref_type);
            invalid = true;
          }
        }
        break;
      }
      default: {
        logerr("Invalid layer type '%c' (%d).\n", layer_type, layer_type);
        invalid = true;
      }
    }
    if (invalid) {
      break;
    }
    if (layer_state.missing_layer_possibly_leading && layer_state.saw_non_missing_layer) {
      layer_state.missing_layer_is_leading = true;
      layer_state.missing_layer_possibly_leading = false;
    }
    if (*nextop != 'E') {
      // Another layer to process, so add a dot separator.
      if (just_processed_layer && layer_state.saw_non_missing_layer) {
        if (!sin_sb_append_cstr(&sb, ".")) invalid = true;
      } else if (just_processed_layer && layer_state.saw_missing_layer && !layer_state.saw_non_missing_layer) {
        // Leading missing layer candidate: don't emit a separator yet.
      }
    } else {
      // End of item definition.
      break;
    }
    just_processed_layer = false;
  }

  if (!invalid && layer_state.saw_missing_layer) {
    if (!layer_state.missing_layer_is_leading || !layer_state.saw_non_missing_layer) {
      invalid = true;
    }
  }

  if (invalid) {
    // Not a valid item name, so push nil.
    sin_sb_dispose(&sb);
    push_stack(VM->stack, VALUE_NIL);
  } else {
    VALUE_t name;
    name.type = VALUE_str;
    name.s = sin_sb_take(&sb); // Don't free - it's on the stack!
    push_stack(VM->stack, name);
    logverbose("Item assembled: %s\n", name.s);
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

void init_interpreter(RuntimeContext *ctx) {
  if (ctx && !ctx->initialized) {
    (void)runtime_init(ctx, ctx->vm);
  }
}

static void restore_outer_transfer(RuntimeContext *ctx, ITEM_t *saved) {
  if (!runtime_frame_restore_transfer(ctx, saved)) {
    logerr("Unable to restore outer pending frame transfer.\n");
  }
}

VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item) {
  if (!ctx) return VALUE_NIL;
  // Lazy initialization must complete before changing any per-invocation
  // state.  A failed allocation here leaves the context untouched and safe
  // to retry on a later invocation.
  if (!ctx->initialized && !runtime_init(ctx, ctx->vm)) return VALUE_NIL;
  RuntimeDecoder saved_decoder = ctx->decoder;
  ITEM_t *saved_current_item = ctx->current_item;
  ITEM_t *saved_pending_call_item = NULL;
  bool had_saved_pending_transfer =
      runtime_frame_take_transfer(ctx, &saved_pending_call_item);
  int saved_invocation_callstack_floor = ctx->invocation_callstack_floor;
  ITEM_t *saved_invocation_caller_item = ctx->invocation_caller_item;
  RuntimeFrameCheckpoint checkpoint;
  if (!runtime_frame_checkpoint(ctx, &checkpoint)) {
    if (had_saved_pending_transfer) {
      restore_outer_transfer(ctx, saved_pending_call_item);
    }
    return VALUE_NIL;
  }
  ctx->invocation_callstack_floor = checkpoint.callstack_depth;
  ctx->invocation_caller_item = saved_current_item;
  clear_error_item(itemstore_root(ctx->itemstore));
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  ctx->current_item = item;
  ctx->interrupted = false;

  if (!verify_runtime_bytecode(ctx, ctx->current_item)) {
    ctx->current_item = saved_current_item;
    restore_outer_transfer(ctx, had_saved_pending_transfer
                                      ? saved_pending_call_item : NULL);
    ctx->invocation_callstack_floor = saved_invocation_callstack_floor;
    ctx->invocation_caller_item = saved_invocation_caller_item;
    ctx->decoder = saved_decoder;
    runtime_frame_restore_ownership(ctx, &checkpoint);
    return VALUE_NIL;
  }
  if (consume_runtime_interrupt(ctx)) {
    ctx->current_item = saved_current_item;
    restore_outer_transfer(ctx, had_saved_pending_transfer
                                      ? saved_pending_call_item : NULL);
    ctx->invocation_callstack_floor = saved_invocation_callstack_floor;
    ctx->invocation_caller_item = saved_invocation_caller_item;
    ctx->decoder = saved_decoder;
    runtime_frame_restore_ownership(ctx, &checkpoint);
    return VALUE_NIL;
  }

  // Enter initial frame.
  const uint8_t *current_code = item_bytecode(ctx->current_item);
  uint32_t current_code_len = item_bytecode_length(ctx->current_item);
  BC_FormatHeader current_header;
  if (!runtime_code_header(ctx->current_item, &current_header)) goto interpretation_failure;
  if (!runtime_frame_enter_initial(ctx, ctx->current_item, current_header.locals,
                                   current_header.params)) {
    set_runtime_bytecode_error(ctx, NULL, 0,
        "unable to enter initial frame: VM stack capacity exhausted");
    goto interpretation_failure;
  }
  uint8_t *op = (uint8_t *)current_header.instructions;
  runtime_decoder_init(&ctx->decoder, op, (uint8_t *)current_code + current_code_len);

  while (true) {
    if (consume_runtime_interrupt(ctx)) goto interpretation_failure;
    if (op < ctx->decoder.frame_start || op >= ctx->decoder.frame_end) {
      const uint8_t *frame_code = ctx->current_item ? item_bytecode(ctx->current_item) : NULL;
      uint32_t offset = frame_code
          ? (uint32_t)(ctx->decoder.frame_end - frame_code) : 0;
      set_runtime_bytecode_error(ctx, NULL, offset,
          "execution reached the bytecode frame boundary without HALT");
      goto interpretation_failure;
    }
    if (*op == 'h' || *op == 'Q') {
      RuntimeFrameReturn returned;
      if (!runtime_frame_return(ctx, &checkpoint, *op == 'Q', &returned)) {
        set_runtime_bytecode_error(ctx, NULL, 0,
            "unable to return from call: VM stack capacity exhausted");
        goto interpretation_failure;
      }
      if (returned.completed) {
        ctx->decoder = saved_decoder;
        ctx->current_item = saved_current_item;
        restore_outer_transfer(ctx, had_saved_pending_transfer
                                          ? saved_pending_call_item : NULL);
        ctx->invocation_callstack_floor = saved_invocation_callstack_floor;
        ctx->invocation_caller_item = saved_invocation_caller_item;
        runtime_frame_restore_ownership(ctx, &checkpoint);
        return returned.result;
      }
      op = returned.nextop;
      runtime_decoder_init(&ctx->decoder, returned.bytecode_start,
                           returned.bytecode_end);
      continue;
    }

    uint8_t *nextop = op + 1;
    uint8_t *newop = ctx->opcode[*op](ctx, nextop, ctx->current_item);
    if (!newop) {
      ITEM_t *transfer_target = NULL;
      if (runtime_frame_take_transfer(ctx, &transfer_target)) {
        ctx->current_item = transfer_target;
        if (!verify_runtime_bytecode(ctx, ctx->current_item)) {
          goto interpretation_failure;
        }
        current_code = item_bytecode(ctx->current_item);
        current_code_len = item_bytecode_length(ctx->current_item);
        if (!runtime_code_header(ctx->current_item, &current_header)) goto interpretation_failure;
        op = (uint8_t *)current_header.instructions;
        runtime_decoder_init(&ctx->decoder, op, (uint8_t *)current_code + current_code_len);
        continue;
      }
interpretation_failure:
      runtime_frame_unwind(ctx, &checkpoint);
      ctx->decoder = saved_decoder;
      ctx->current_item = saved_current_item;
      restore_outer_transfer(ctx, had_saved_pending_transfer
                                        ? saved_pending_call_item : NULL);
      ctx->invocation_callstack_floor = saved_invocation_callstack_floor;
      ctx->invocation_caller_item = saved_invocation_caller_item;
      runtime_frame_restore_ownership(ctx, &checkpoint);
      return VALUE_NIL;
    }
    op = newop;
  }
}
