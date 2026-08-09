// Checked runtime frame transitions.

// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <stdint.h>

#include "item.h"
#include "log.h"
#include "runtime_context.h"
#include "runtime_frame.h"
#include "stack.h"

static bool frame_push_callstack(VM_t *vm, ITEM_t *item, uint8_t *nextop,
                                 uint8_t args, uint8_t locals,
                                 uint8_t *bytecode_start,
                                 uint8_t *bytecode_end) {
  if (!vm || !vm->stack || !vm->callstack || args > locals ||
      vm->stack->current < (int32_t)args - 1 ||
      vm->callstack->current >= vm->callstack->max ||
      vm->stack->current > vm->stack->max - (int32_t)(locals - args)) {
    return false;
  }
  vm->callstack->current++;
  vm->callstack->entry[vm->callstack->current].item = item;
  vm->callstack->entry[vm->callstack->current].nextop = nextop;
  vm->callstack->entry[vm->callstack->current].bytecode_start = bytecode_start;
  vm->callstack->entry[vm->callstack->current].bytecode_end = bytecode_end;
  vm->callstack->entry[vm->callstack->current].current_stack =
      vm->stack->current - args;
  vm->callstack->entry[vm->callstack->current].current_base = vm->stack->base;
  vm->callstack->entry[vm->callstack->current].current_locals =
      vm->stack->locals;
  vm->callstack->entry[vm->callstack->current].current_params =
      vm->stack->params;
  vm->stack->base = vm->stack->current + 1 - args;
  vm->stack->current += (int32_t)locals - args;
  vm->stack->locals = locals;
  vm->stack->params = args;
  return true;
}

static FRAME_t *frame_pop_callstack(VM_t *vm) {
  if (!vm || !vm->stack || !vm->callstack || vm->callstack->current < 0) {
    logerr("Callstack underflow.\n");
    return NULL;
  }
  reset_stack_to(vm->stack,
      vm->callstack->entry[vm->callstack->current].current_stack);
  vm->stack->locals = vm->callstack->entry[vm->callstack->current].current_locals;
  vm->stack->params = vm->callstack->entry[vm->callstack->current].current_params;
  vm->stack->base = vm->callstack->entry[vm->callstack->current].current_base;
  vm->callstack->current--;
  return &vm->callstack->entry[vm->callstack->current + 1];
}

bool runtime_frame_checkpoint(RuntimeContext *ctx,
                              RuntimeFrameCheckpoint *checkpoint) {
  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack ||
      !checkpoint) return false;
  checkpoint->callstack_depth = size_callstack(ctx->vm->callstack);
  checkpoint->stack_current = ctx->vm->stack->current;
  checkpoint->stack_base = ctx->vm->stack->base;
  checkpoint->stack_locals = ctx->vm->stack->locals;
  checkpoint->stack_params = ctx->vm->stack->params;
  checkpoint->prior_owned_frames = ctx->frame_owned_count;
  ctx->frame_owned_count = 0u;
  return true;
}

bool runtime_frame_enter_initial(RuntimeContext *ctx, ITEM_t *item,
                                 uint8_t locals, uint8_t params) {
  if (!ctx || !ctx->vm || !ctx->vm->stack || !item || params > locals ||
      ctx->vm->stack->current >
          ctx->vm->stack->max - (int32_t)(locals - params)) {
    logerr("Unable to enter initial frame: insufficient VM stack capacity.\n");
    return false;
  }
  ctx->vm->stack->current += (int32_t)locals - params;
  ctx->vm->stack->locals = locals;
  ctx->vm->stack->params = params;
  item_enter_use(item);
  ctx->frame_owned_count++;
  return true;
}

static bool frame_call_capacity(const VM_t *vm, int32_t current,
                                size_t supplied, uint8_t locals,
                                uint8_t params, bool require_arguments) {
  size_t effective = supplied < params ? supplied : params;
  size_t excess = supplied - effective;
  size_t missing = (size_t)params - effective;
  int64_t normalized_current = (int64_t)current - (int64_t)excess +
                               (int64_t)missing;
  int64_t available = (int64_t)current + 1;
  if (!vm || !vm->stack || !vm->callstack || params > locals ||
      (require_arguments && (available < 0 ||
                             (uint64_t)supplied > (uint64_t)available)) ||
      vm->callstack->current >= vm->callstack->max ||
      normalized_current > (int64_t)vm->stack->max -
          (int64_t)(locals - params)) return false;
  return true;
}

bool runtime_frame_preflight_call(RuntimeContext *ctx, size_t supplied,
                                  uint8_t locals, uint8_t params,
                                  size_t *effective_count) {
  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack ||
      params > locals || !effective_count) return false;
  size_t effective = supplied < params ? supplied : params;
  if (ctx->vm->stack->current > ctx->vm->stack->max - (int32_t)effective ||
      !frame_call_capacity(ctx->vm, ctx->vm->stack->current +
                           (int32_t)effective, effective, locals, params,
                           false)) return false;
  *effective_count = effective;
  return true;
}

bool runtime_frame_prepare_call(RuntimeContext *ctx, ITEM_t *caller,
                                uint8_t *nextop, ITEM_t *target,
                                size_t supplied, uint8_t locals,
                                uint8_t params, uint8_t *bytecode_start,
                                uint8_t *bytecode_end,
                                size_t *discarded_count) {
  VM_t *vm;
  STACK_t *stack;
  size_t effective;
  size_t discarded;

  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack ||
      !caller || !target || params > locals) {
    logerr("Unable to enter call frame: insufficient VM capacity or invalid arguments.\n");
    return false;
  }
  vm = ctx->vm;
  stack = vm->stack;
  effective = supplied < params ? supplied : params;
  discarded = supplied - effective;
  if (!frame_call_capacity(vm, stack->current, supplied, locals, params,
                           true)) {
    logerr("Unable to enter call frame: insufficient VM capacity or invalid arguments.\n");
    return false;
  }

  while (supplied > effective) {
    VALUE_t discarded = pop_stack(stack);
    value_free(&discarded);
    supplied--;
  }
  while (supplied < params) {
    push_stack(stack, VALUE_NIL);
    supplied++;
  }
  if (!frame_push_callstack(vm, caller, nextop, params, locals,
                            bytecode_start, bytecode_end)) {
    /* The private helper is checked, so this is defensive if its contract changes. */
    return false;
  }
  item_enter_use(target);
  ctx->pending_call_item = target;
  ctx->frame_owned_count++;
  if (discarded_count) *discarded_count = discarded;
  return true;
}

bool runtime_frame_take_transfer(RuntimeContext *ctx, ITEM_t **target) {
  if (!ctx || !target || !ctx->pending_call_item) return false;
  *target = ctx->pending_call_item;
  ctx->pending_call_item = NULL;
  return true;
}

ITEM_t *runtime_frame_pending_transfer(const RuntimeContext *ctx) {
  return ctx ? ctx->pending_call_item : NULL;
}

bool runtime_frame_restore_transfer(RuntimeContext *ctx, ITEM_t *target) {
  if (!ctx || (ctx->pending_call_item &&
               ctx->pending_call_item != target)) return false;
  ctx->pending_call_item = target;
  return true;
}

static VALUE_t pop_frame_result(STACK_t *stack, bool explicit_return) {
  int32_t frame_value_floor = stack->base + stack->locals - 1;
  VALUE_t result = VALUE_NIL;
  if (explicit_return && stack->current > frame_value_floor) {
    result = pop_stack(stack);
  }
  while (stack->current > frame_value_floor) {
    VALUE_t discarded = pop_stack(stack);
    value_free(&discarded);
  }
  return result;
}

bool runtime_frame_return(RuntimeContext *ctx,
                          const RuntimeFrameCheckpoint *checkpoint,
                          bool explicit_return, RuntimeFrameReturn *returned) {
  VM_t *vm;
  VALUE_t result;

  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack ||
      !checkpoint || !returned || !ctx->current_item) return false;
  vm = ctx->vm;
  result = pop_frame_result(vm->stack, explicit_return);
  item_leave_use(ctx->current_item);
  if (ctx->frame_owned_count > 0u) ctx->frame_owned_count--;
  returned->result = result;
  returned->completed = size_callstack(vm->callstack) == checkpoint->callstack_depth;
  returned->caller_item = NULL;
  returned->nextop = NULL;
  returned->bytecode_start = NULL;
  returned->bytecode_end = NULL;
  if (returned->completed) {
    reset_stack_to(vm->stack, checkpoint->stack_current);
    vm->stack->base = checkpoint->stack_base;
    vm->stack->locals = checkpoint->stack_locals;
    vm->stack->params = checkpoint->stack_params;
    return true;
  }

  FRAME_t *frame = frame_pop_callstack(vm);
  if (!frame) {
    value_free(&returned->result);
    return false;
  }
  returned->caller_item = frame->item;
  returned->nextop = frame->nextop;
  returned->bytecode_start = frame->bytecode_start;
  returned->bytecode_end = frame->bytecode_end;
  ctx->current_item = frame->item;
  if (vm->stack->current >= vm->stack->max) {
    value_free(&returned->result);
    return false;
  }
  push_stack(vm->stack, returned->result);
  returned->result = VALUE_NIL;
  return true;
}

void runtime_frame_unwind(RuntimeContext *ctx,
                          const RuntimeFrameCheckpoint *checkpoint) {
  VM_t *vm;
  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack ||
      !checkpoint) return;
  vm = ctx->vm;
  if (ctx->pending_call_item) {
    item_leave_use(ctx->pending_call_item);
    ctx->pending_call_item = NULL;
    if (ctx->frame_owned_count > 0u) ctx->frame_owned_count--;
  } else if (ctx->frame_owned_count > 0u && ctx->current_item) {
    item_leave_use(ctx->current_item);
    ctx->frame_owned_count--;
  }
  while (size_callstack(vm->callstack) > checkpoint->callstack_depth) {
    FRAME_t *frame = frame_pop_callstack(vm);
    if (!frame) break;
    if (frame->item) item_leave_use(frame->item);
    if (ctx->frame_owned_count > 0u) ctx->frame_owned_count--;
  }
  reset_stack_to(vm->stack, checkpoint->stack_current);
  vm->stack->base = checkpoint->stack_base;
  vm->stack->locals = checkpoint->stack_locals;
  vm->stack->params = checkpoint->stack_params;
}

void runtime_frame_restore_ownership(RuntimeContext *ctx,
                                     const RuntimeFrameCheckpoint *checkpoint) {
  if (ctx && checkpoint) ctx->frame_owned_count = checkpoint->prior_owned_frames;
}
