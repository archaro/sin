#include "item.h"
#include <stdint.h>
#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "interpret.h"
#include "item.h"
#include "runtime_value.h"
#include "runtime_frame.h"
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
  code[pos++] = 'Q';
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
  code[pos++] = 'Q';
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
  code[pos++] = 'Q';
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

static ITEM_t *insert_large_stack_caller(const char *name, const char *target,
                                         bool use_sys_call) {
  const size_t filler_count = 780;
  size_t capacity = 2 + filler_count * 8 + 64;
  uint8_t *code = calloc(capacity, 1);
  ASSERT_NOT_NULL(code);
  size_t pos = 2;
  for (size_t i = 0; i < filler_count; i++) {
    code[pos++] = 'l';
    pos = emit_string(code, pos, "x");
  }
  code[pos++] = 'l';
  pos = emit_string(code, pos, target);
  if (use_sys_call) {
    code[pos++] = 'M'; code[pos++] = 1; code[pos++] = 21;
    code[pos++] = '['; memset(code + pos, 0, 4); pos += 4;
    code[pos++] = 'M'; code[pos++] = 1; code[pos++] = 24;
  } else {
    code[pos++] = 'F'; code[pos++] = 0; code[pos++] = 0;
  }
  code[pos++] = 'h';
  ITEM_t *item = insert_frame_code(name, code, pos);
  free(code);
  return item;
}

static ITEM_t *insert_large_local_recursive(const char *name) {
  uint8_t code[96] = {255, 0};
  size_t pos = 2;
  code[pos++] = 'l';
  pos = emit_string(code, pos, name);
  code[pos++] = 'F'; code[pos++] = 0; code[pos++] = 0;
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

void test_transactional_frame_entry_rejects_stack_and_callstack_overflow(void) {
  VM_t *vm = make_vm();
  RuntimeContext ctx;
  RuntimeFrameCheckpoint checkpoint;
  ITEM_t *frame_item;
  ASSERT_NOT_NULL(vm);
  frame_item = make_root_item("frame");
  ASSERT_NOT_NULL(frame_item);
  runtime_context_init(&ctx, vm);
  vm->stack->current = vm->stack->max - 1;
  int32_t stack_before = vm->stack->current;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(!runtime_frame_prepare_call(&ctx, frame_item, NULL, frame_item,
                                           0, 3, 0, NULL, NULL, NULL));
  ASSERT_EQ_INT(stack_before, vm->stack->current);
  ASSERT_EQ_INT(-1, vm->callstack->current);

  vm->stack->current = -1;
  vm->callstack->current = vm->callstack->max;
  ASSERT_TRUE(!runtime_frame_prepare_call(&ctx, frame_item, NULL, frame_item,
                                           0, 0, 0, NULL, NULL, NULL));
  ASSERT_EQ_INT(vm->callstack->max, vm->callstack->current);
  vm->callstack->current = -1;
  ctx.current_item = frame_item;
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, frame_item, 255, 0));
  ASSERT_EQ_INT(254, vm->stack->current);
  vm->stack->current = vm->stack->max - 1;
  ASSERT_TRUE(!runtime_frame_enter_initial(&ctx, frame_item, 255, 0));
  ASSERT_EQ_INT(vm->stack->max - 1, vm->stack->current);
  runtime_frame_unwind(&ctx, &checkpoint);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  destroy_vm(vm);
  destroy_item(frame_item);
}

void test_runtime_frame_direct_lifecycle_restores_state(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  ITEM_t *caller = insert_frame_code("frames.api_caller",
                                     (uint8_t[]){0, 0, 'h'}, 3);
  ITEM_t *callee = insert_frame_code("frames.api_callee",
                                     (uint8_t[]){3, 0, 'h'}, 3);
  RuntimeContext ctx;
  RuntimeFrameCheckpoint checkpoint;
  RuntimeFrameReturn returned;
  ITEM_t *transfer = NULL;
  size_t discarded = 0u;
  runtime_context_init(&ctx, config.vm);
  ctx.current_item = caller;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 1, 0));
  ASSERT_TRUE(item_is_in_use(caller));
  size_t effective = 0u;
  ASSERT_TRUE(runtime_frame_preflight_call(&ctx, 2, 3, 3, &effective));
  ASSERT_EQ_INT(2, effective);
  push_stack(config.vm->stack, VALUE_TRUE);
  push_stack(config.vm->stack, VALUE_FALSE);
  ASSERT_TRUE(runtime_frame_prepare_call(
      &ctx, caller, NULL, callee, 2, 3, 3, NULL, NULL, &discarded));
  ASSERT_EQ_INT(0, discarded);
  ASSERT_EQ_INT(3, config.vm->stack->current);
  ASSERT_EQ_INT(0, config.vm->callstack->current);
  ASSERT_TRUE(item_is_in_use(callee));
  ASSERT_TRUE(runtime_frame_take_transfer(&ctx, &transfer));
  ASSERT_TRUE(transfer == callee);
  ctx.current_item = transfer;
  push_stack(config.vm->stack,
             concat_two_strings((VALUE_t){VALUE_str, {.s = strdup("ret")}},
                                (VALUE_t){VALUE_str, {.s = strdup("")}}));
  ASSERT_TRUE(runtime_frame_return(&ctx, &checkpoint, true, &returned));
  ASSERT_TRUE(!returned.completed);
  ASSERT_TRUE(ctx.current_item == caller);
  ASSERT_TRUE(!item_is_in_use(callee));
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "ret") == 0);
  value_free(&result);
  ASSERT_TRUE(runtime_frame_return(&ctx, &checkpoint, false, &returned));
  ASSERT_TRUE(returned.completed);
  ASSERT_TRUE(!item_is_in_use(caller));
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  teardown_stack_frame_runtime();
}

void test_runtime_frame_normalizes_more_than_255_arguments(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  ITEM_t *caller = insert_frame_code("frames.api_many_caller",
                                     (uint8_t[]){0, 0, 'h'}, 3);
  ITEM_t *callee = insert_frame_code("frames.api_many_callee",
                                     (uint8_t[]){1, 0, 'h'}, 3);
  RuntimeContext ctx;
  RuntimeFrameCheckpoint checkpoint;
  RuntimeFrameReturn returned;
  ITEM_t *transfer = NULL;
  size_t discarded = 0u;
  runtime_context_init(&ctx, config.vm);
  ctx.current_item = caller;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 0, 0));
  for (size_t index = 0u; index < 300u; index++) {
    push_stack(config.vm->stack, VALUE_TRUE);
  }
  for (size_t index = 0u; index < 600u; index++) {
    push_stack(config.vm->stack,
               concat_two_strings((VALUE_t){VALUE_str, {.s = strdup("x")}},
                                  (VALUE_t){VALUE_str, {.s = strdup("")}}));
  }
  ASSERT_TRUE(runtime_frame_prepare_call(
      &ctx, caller, NULL, callee, 600u, 255, 1, NULL, NULL, &discarded));
  ASSERT_EQ_INT(599, discarded);
  ASSERT_EQ_INT(554, config.vm->stack->current);
  ASSERT_TRUE(runtime_frame_take_transfer(&ctx, &transfer));
  ctx.current_item = transfer;
  ASSERT_TRUE(runtime_frame_return(&ctx, &checkpoint, false, &returned));
  ASSERT_TRUE(!returned.completed);
  ASSERT_TRUE(!item_is_in_use(callee));
  ASSERT_TRUE(runtime_frame_return(&ctx, &checkpoint, false, &returned));
  ASSERT_TRUE(returned.completed);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  ASSERT_TRUE(!item_is_in_use(caller));
  teardown_stack_frame_runtime();
}

void test_runtime_frame_failure_ownership_and_return_capacity(void) {
  setup_stack_frame_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  ITEM_t *caller = insert_frame_code("frames.api_failure_caller",
                                     (uint8_t[]){0, 0, 'h'}, 3);
  ITEM_t *outer = insert_frame_code("frames.api_failure_outer",
                                    (uint8_t[]){0, 0, 'h'}, 3);
  RuntimeContext ctx;
  RuntimeFrameCheckpoint checkpoint;
  RuntimeFrameReturn returned;
  ITEM_t *transfer = NULL;
  size_t discarded = 0u;
  runtime_context_init(&ctx, config.vm);
  ctx.current_item = caller;

  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 0, 0));
  VALUE_t owned = concat_two_strings(
      (VALUE_t){VALUE_str, {.s = strdup("owned")}},
      (VALUE_t){VALUE_str, {.s = strdup(" argument")}});
  push_stack(config.vm->stack, owned);
  VALUE_t *saved_argument = peek_stack(config.vm->stack);
  ASSERT_NOT_NULL(saved_argument);
  config.vm->callstack->current = config.vm->callstack->max;
  ASSERT_TRUE(!runtime_frame_prepare_call(
      &ctx, caller, NULL, caller, 1, 1, 1, NULL, NULL, &discarded));
  ASSERT_TRUE(peek_stack(config.vm->stack) == saved_argument);
  ASSERT_EQ_INT(0, discarded);
  ASSERT_EQ_INT(config.vm->callstack->max, config.vm->callstack->current);
  ASSERT_TRUE(runtime_frame_pending_transfer(&ctx) == NULL);
  ASSERT_TRUE(item_is_in_use(caller));
  reset_stack_to(config.vm->stack, -1);
  config.vm->callstack->current = -1;
  runtime_frame_unwind(&ctx, &checkpoint);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());

  item_enter_use(outer);
  runtime_context_init(&ctx, config.vm);
  ctx.current_item = caller;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 0, 0));
  push_stack(config.vm->stack, VALUE_TRUE);
  ASSERT_TRUE(runtime_frame_prepare_call(
      &ctx, caller, NULL, caller, 1, 1, 1, NULL, NULL, &discarded));
  ASSERT_TRUE(runtime_frame_take_transfer(&ctx, &transfer));
  ctx.current_item = transfer;
  ASSERT_TRUE(item_is_in_use(caller));
  runtime_frame_unwind(&ctx, &checkpoint);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(item_is_in_use(outer));
  item_leave_use(outer);

  runtime_context_init(&ctx, config.vm);
  ctx.current_item = caller;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 0, 0));
  config.vm->stack->current = config.vm->stack->max;
  ASSERT_TRUE(runtime_frame_prepare_call(
      &ctx, caller, NULL, outer, 0, 0, 0, NULL, NULL, &discarded));
  ASSERT_TRUE(runtime_frame_take_transfer(&ctx, &transfer));
  ctx.current_item = transfer;
  ASSERT_TRUE(!runtime_frame_return(&ctx, &checkpoint, false, &returned));
  ASSERT_TRUE(!item_is_in_use(outer));
  ASSERT_TRUE(item_is_in_use(caller));
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  runtime_frame_unwind(&ctx, &checkpoint);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  teardown_stack_frame_runtime();
}

void test_runtime_frame_nested_invocation_preserves_pending_transfer(void) {
  setup_stack_frame_runtime();
  ITEM_t *caller = insert_frame_code("frames.api_nested_caller",
                                     (uint8_t[]){0, 0, 'h'}, 3);
  ITEM_t *callee = insert_frame_code("frames.api_nested_callee",
                                     (uint8_t[]){0, 0, 'h'}, 3);
  ITEM_t *nested_caller = insert_large_stack_caller(
      "frames.api_nested_runner", "frames.api_nested_fail_target", false);
  ITEM_t *nested_fail_target = insert_frame_code(
      "frames.api_nested_fail_target", (uint8_t[]){255, 0, 'h'}, 3);
  RuntimeContext ctx;
  RuntimeFrameCheckpoint checkpoint;
  ITEM_t *transfer = NULL;
  size_t discarded = 0u;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ctx.current_item = caller;
  ASSERT_TRUE(runtime_frame_checkpoint(&ctx, &checkpoint));
  ASSERT_TRUE(runtime_frame_enter_initial(&ctx, caller, 0, 0));
  ASSERT_TRUE(runtime_frame_prepare_call(
      &ctx, caller, NULL, callee, 0, 0, 0, NULL, NULL, &discarded));
  ASSERT_TRUE(runtime_frame_pending_transfer(&ctx) == callee);
  ASSERT_TRUE(item_is_in_use(caller));
  ASSERT_TRUE(item_is_in_use(callee));

  VALUE_t nested = interpret(&ctx, nested_caller);
  ASSERT_EQ_INT(VALUE_nil, nested.type);
  value_free(&nested);
  ASSERT_TRUE(runtime_frame_pending_transfer(&ctx) == callee);
  ASSERT_EQ_INT(0, config.vm->callstack->current);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_TRUE(item_is_in_use(caller));
  ASSERT_TRUE(item_is_in_use(callee));
  ASSERT_TRUE(!item_is_in_use(nested_caller));
  ASSERT_TRUE(!item_is_in_use(nested_fail_target));

  ASSERT_TRUE(runtime_frame_take_transfer(&ctx, &transfer));
  ctx.current_item = transfer;
  runtime_frame_unwind(&ctx, &checkpoint);
  runtime_frame_restore_ownership(&ctx, &checkpoint);
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(!item_is_in_use(callee));
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  teardown_stack_frame_runtime();
}

void test_large_local_direct_and_sys_call_rejection_reuses_vm(void) {
  setup_stack_frame_runtime();
  ITEM_t *recursive = insert_large_local_recursive("frames.large_recursive");
  ITEM_t *sys_target = insert_frame_code("frames.large_sys_target",
      (uint8_t[]){255, 0, 'h'}, 3);
  ITEM_t *sys = insert_large_stack_caller("frames.large_sys",
      "frames.large_sys_target", true);

  VALUE_t result = run_frame_item(recursive);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_TRUE(!item_is_in_use(recursive));

  result = run_frame_item(sys);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  ASSERT_TRUE(!item_is_in_use(sys));
  ASSERT_TRUE(!item_is_in_use(sys_target));

  result = run_frame_item(sys_target);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  teardown_stack_frame_runtime();
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
  /* Seed operands so the intercepted ADD is verifier-valid. */
  uint8_t callee_code[] = {0, 0, 'b', 1, 'b', 1, 'a', 'h'};
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
