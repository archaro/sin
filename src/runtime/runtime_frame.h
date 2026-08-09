// Checked runtime frame transitions.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "value.h"
#include "vm.h"

typedef struct RuntimeContext RuntimeContext;

typedef struct {
  int callstack_depth;
  int32_t stack_current;
  int32_t stack_base;
  uint8_t stack_locals;
  uint8_t stack_params;
  size_t prior_owned_frames;
} RuntimeFrameCheckpoint;

typedef struct {
  bool completed;
  ITEM_t *caller_item;
  uint8_t *nextop;
  uint8_t *bytecode_start;
  uint8_t *bytecode_end;
  VALUE_t result;
} RuntimeFrameReturn;

/*
 * Capture the VM boundary that an invocation must restore. This resets the
 * invocation-owned pin accounting; every successful checkpoint must be paired
 * with runtime_frame_restore_ownership() on both return and failure paths.
 */
bool runtime_frame_checkpoint(RuntimeContext *ctx,
                              RuntimeFrameCheckpoint *checkpoint);

/* Enter an initial frame. The frame, not the item, owns one pin on success. */
bool runtime_frame_enter_initial(RuntimeContext *ctx, ITEM_t *item,
                                 uint8_t locals, uint8_t params);

/*
 * Read-only capacity check for staging effective arguments before cloning
 * them onto the VM stack. effective_count receives min(supplied, params) on
 * success and is unchanged on failure.
 */
bool runtime_frame_preflight_call(RuntimeContext *ctx, size_t supplied,
                                  uint8_t locals, uint8_t params,
                                  size_t *effective_count);

/*
 * Prepare and publish a code-item transfer. All capacity and arity checks are
 * completed before arguments are changed or the continuation/pin is visible.
 * The active frame invocation owns one execution pin after success;
 * take_transfer() consumes the pending transfer without changing that
 * ownership.
 * discarded_count, when non-NULL, receives the excess argument count after
 * all checks succeed; it is unchanged on failure. On false, no stack,
 * callstack, transfer, or pin state is published.
 */
bool runtime_frame_prepare_call(RuntimeContext *ctx, ITEM_t *caller,
                                uint8_t *nextop, ITEM_t *target,
                                size_t supplied, uint8_t locals,
                                uint8_t params, uint8_t *bytecode_start,
                                uint8_t *bytecode_end,
                                size_t *discarded_count);
bool runtime_frame_take_transfer(RuntimeContext *ctx, ITEM_t **target);
/* Nested callers use these accessors to save/restore an unconsumed transfer. */
ITEM_t *runtime_frame_pending_transfer(const RuntimeContext *ctx);
/* Restoring may only publish a separately saved outer transfer; it refuses to
 * overwrite a different active, invocation-owned transfer. */
bool runtime_frame_restore_transfer(RuntimeContext *ctx, ITEM_t *target);

/*
 * Return from the current frame and place its result in the caller frame.
 * On true, result is either returned to the caller in the VM stack (with
 * returned->result set to nil) or owned by the caller for a completed
 * invocation. On false, any result is consumed, and the interpreter must call
 * runtime_frame_unwind() before restoring the checkpoint ownership.
 */
bool runtime_frame_return(RuntimeContext *ctx,
                          const RuntimeFrameCheckpoint *checkpoint,
                          bool explicit_return, RuntimeFrameReturn *returned);

/* Release all frame pins and restore the invocation checkpoint. */
void runtime_frame_unwind(RuntimeContext *ctx,
                          const RuntimeFrameCheckpoint *checkpoint);
/* Restore the outer invocation's pin accounting after return or unwind. */
void runtime_frame_restore_ownership(RuntimeContext *ctx,
                                     const RuntimeFrameCheckpoint *checkpoint);
