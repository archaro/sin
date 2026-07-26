#include "item.h"
#include <stdint.h>
#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "interpret.h"
#include "item.h"
#include "runtime_value.h"
#include "stack.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

extern CONFIG_t config;

static void setup_stack_frame_runtime(void) {
  memset(&config, 0, sizeof(config));
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_stack_frame_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
  memset(&config, 0, sizeof(config));
}

static size_t emit_string(uint8_t *code, size_t pos, const char *value) {
  uint16_t length = (uint16_t)strlen(value);
  memcpy(code + pos, &length, sizeof(length));
  pos += sizeof(length);
  memcpy(code + pos, value, length);
  return pos + length;
}

static ITEM_t *insert_frame_code(const char *name, const uint8_t *code,
                                 size_t length) {
  uint8_t *owned = malloc(length);
  ASSERT_NOT_NULL(owned);
  memcpy(owned, code, length);
  ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx), name, (uint32_t)length,
                                  owned);
  ASSERT_NOT_NULL(item);
  return item;
}

static VALUE_t run_frame_item(ITEM_t *item) {
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  return interpret(&ctx, item);
}

static uint8_t *interrupt_in_callee(RuntimeContext *ctx, uint8_t *nextop,
                                    ITEM_t *item) {
  (void)item;
  ASSERT_NOT_NULL(ctx->interrupt_pending);
  *ctx->interrupt_pending = 1;
  return nextop;
}

static ITEM_t *insert_runner(const char *name, const char *target) {
  uint8_t code[64] = {0};
  size_t pos = 2;
  code[0] = 0;
  code[1] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, target);
  code[pos++] = 'F';
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'h';
  return insert_frame_code(name, code, pos);
}

static ITEM_t *insert_string_local_frame(const char *name,
                                          const char *local_text,
                                          const char *temporary_text) {
  uint8_t code[128] = {0};
  size_t pos = 2;
  code[0] = 1;
  code[1] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, local_text);
  code[pos++] = 'l';
  pos = emit_string(code, pos, temporary_text);
  code[pos++] = 'a';
  code[pos++] = 'c';
  code[pos++] = 0;
  code[pos++] = 'e';
  code[pos++] = 0;
  code[pos++] = 'h';
  return insert_frame_code(name, code, pos);
}

static ITEM_t *insert_outer_string_frame(const char *name, const char *target) {
  uint8_t code[128] = {0};
  size_t pos = 2;
  code[0] = 1;
  code[1] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, "outer-");
  code[pos++] = 'c';
  code[pos++] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, target);
  code[pos++] = 'F';
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'e';
  code[pos++] = 0;
  code[pos++] = 'a';
  code[pos++] = 'h';
  return insert_frame_code(name, code, pos);
}

static ITEM_t *insert_outer_nil_frame(const char *name, const char *target) {
  uint8_t code[96] = {0};
  size_t pos = 2;
  code[0] = 1;
  code[1] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, "outer-nil");
  code[pos++] = 'c';
  code[pos++] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, target);
  code[pos++] = 'F';
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'h';
  return insert_frame_code(name, code, pos);
}

void test_stack_reset_to_frees_values_at_boundaries(void) {
  STACK_t *stack = make_stack();
  ASSERT_NOT_NULL(stack);
  size_t baseline = strbuf_tracked_count_for_tests();

  reset_stack_to(stack, -1);
  ASSERT_EQ_INT(-1, stack->current);

  push_stack(stack, VALUE_TRUE);
  int32_t current = stack->current;
  reset_stack_to(stack, current);
  ASSERT_EQ_INT(current, stack->current);

  VALUE_t tracked = concat_two_strings(
      (VALUE_t){VALUE_str, {.s = strdup("left")}},
      (VALUE_t){VALUE_str, {.s = strdup(" right")} });
  ASSERT_EQ_INT(VALUE_str, tracked.type);
  push_stack(stack, tracked);
  ASSERT_EQ_INT((long long)baseline + 1,
                (long long)strbuf_tracked_count_for_tests());
  int32_t lower_top = stack->current - 1;
  reset_stack_to(stack, lower_top);
  ASSERT_EQ_INT(lower_top, stack->current);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());

  reset_stack(stack);
  ASSERT_EQ_INT(-1, stack->current);
  destroy_stack(stack);
}

void test_nested_string_frames_release_locals_and_preserve_result(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  insert_string_local_frame("frames.inner", "inner", " temporary");
  ITEM_t *outer = insert_outer_string_frame("frames.outer", "frames.inner");
  ITEM_t *runner = insert_runner("frames.runner", "frames.outer");

  VALUE_t result = run_frame_item(runner);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "inner temporaryouter-") == 0);
  value_free(&result);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_TRUE(!item_is_in_use(outer));
  teardown_stack_frame_runtime();
}

void test_nested_nil_return_releases_frame_locals(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  uint8_t nil_frame[] = {1, 0, 'l', 4, 0, 'n', 'i', 'l', ' ', 'c', 0, 'h'};
  insert_frame_code("frames.nil", nil_frame, sizeof(nil_frame));
  ITEM_t *outer = insert_outer_nil_frame("frames.outer_nil", "frames.nil");
  ITEM_t *runner = insert_runner("frames.nil_runner", "frames.outer_nil");

  VALUE_t result = run_frame_item(runner);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_TRUE(!item_is_in_use(outer));
  teardown_stack_frame_runtime();
}

void test_top_level_string_frame_cleanup_and_vm_reuse(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  uint8_t code[64] = {0};
  size_t pos = 2;
  code[0] = 1;
  code[1] = 0;
  code[pos++] = 'l';
  pos = emit_string(code, pos, "top-local");
  code[pos++] = 'l';
  pos = emit_string(code, pos, "temp!");
  code[pos++] = 'a';
  code[pos++] = 'c';
  code[pos++] = 0;
  code[pos++] = 'h';
  ITEM_t *item = insert_frame_code("frames.top", code, pos);

  for (int iteration = 0; iteration < 100; iteration++) {
    VALUE_t result = run_frame_item(item);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    value_free(&result);
    ASSERT_EQ_INT((long long)baseline,
                  (long long)strbuf_tracked_count_for_tests());
    ASSERT_EQ_INT(-1, config.vm->stack->current);
    ASSERT_EQ_INT(-1, config.vm->callstack->current);
  }
  teardown_stack_frame_runtime();
}

void test_deferred_interrupt_unwinds_nested_call_frames(void) {
  setup_stack_frame_runtime();
  uint8_t callee_code[] = {0, 0, 'a', 'h'};
  ITEM_t *callee = insert_frame_code("frames.interrupt_callee", callee_code,
                                     sizeof(callee_code));
  ITEM_t *caller = insert_runner("frames.interrupt_caller",
                                 "frames.interrupt_callee");
  ITEM_t *preexisting = insert_frame_code("frames.preexisting",
                                          (uint8_t[]){0, 0, 'h'}, 3);

  STACK_t *stack = config.vm->stack;
  stack->current = 2;
  stack->base = 1;
  stack->locals = 7;
  stack->params = 3;
  config.vm->callstack->current = 0;
  config.vm->callstack->entry[0].item = preexisting;

  volatile sig_atomic_t interrupt_pending = 0;
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ctx.current_item = preexisting;
  ctx.invocation_callstack_floor = 17;
  ctx.invocation_caller_item = preexisting;
  ctx.interrupt_pending = &interrupt_pending;
  ASSERT_TRUE(runtime_init(&ctx, config.vm));
  ctx.opcode[(uint8_t)'a'] = interrupt_in_callee;

  VALUE_t result = interpret(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(ctx.interrupted);
  ASSERT_EQ_INT(0, interrupt_pending);
  ASSERT_EQ_INT(1, size_callstack(config.vm->callstack));
  ASSERT_TRUE(config.vm->callstack->entry[0].item == preexisting);
  ASSERT_EQ_INT(2, stack->current);
  ASSERT_EQ_INT(1, stack->base);
  ASSERT_EQ_INT(7, stack->locals);
  ASSERT_EQ_INT(3, stack->params);
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(!item_is_in_use(callee));
  ASSERT_TRUE(ctx.current_item == preexisting);
  ASSERT_EQ_INT(17, ctx.invocation_callstack_floor);
  ASSERT_TRUE(ctx.invocation_caller_item == preexisting);

  ctx.invocation_callstack_floor = 23;
  ctx.invocation_caller_item = caller;
  interrupt_pending = 1;
  result = interpret(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(ctx.interrupted);
  ASSERT_EQ_INT(0, interrupt_pending);
  ASSERT_EQ_INT(1, size_callstack(config.vm->callstack));
  ASSERT_TRUE(ctx.current_item == preexisting);
  ASSERT_EQ_INT(23, ctx.invocation_callstack_floor);
  ASSERT_TRUE(ctx.invocation_caller_item == caller);

  runtime_destroy(&ctx);
  config.vm->callstack->current = -1;
  stack->current = -1;
  teardown_stack_frame_runtime();
}
