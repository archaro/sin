// Runtime interpreter context

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <signal.h>
#include <uv.h>

#include "item.h"
#include "vm.h"
#include "runtime_decode.h"
#include "network.h"
#include "libcall_registry.h"

typedef struct RuntimeContext RuntimeContext;

/* Runtime bytecode verification is a small per-context cache. Entries hold
 * borrowed pointers and are valid only for their owner and store revisions. */
#define RUNTIME_VERIFY_CACHE_SIZE 8u
#define RUNTIME_VERIFY_POLICY_ID 1u

typedef struct {
  bool valid;
  const uint8_t *bytecode;
  uint32_t bytecode_length;
  ITEMSTORE_t *owner;
  uint64_t topology_revision_epoch;
  uint64_t payload_revision;
  uint64_t payload_revision_epoch;
  uint32_t policy_id;
} RuntimeVerifyCacheEntry;

// Opcode functions have this form. The runtime context owns the VM and
// per-invocation interpreter state that handlers need while executing. A
// context, its VM, and its itemstore are confined to their owning event-loop
// thread; separate contexts do not make concurrent calls supported.
typedef uint8_t *(*OP_t)(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

struct RuntimeContext {
  // Borrowed runtime dependencies supplied by process startup. Runtime
  // execution mutates the VM stack/callstack, the item tree contents, the
  // event loop handles, *safe_shutdown, and *shutdown_requested, but does not
  // own or free these pointers or strings. The network pointer is borrowed;
  // its owner is the process startup state.
  VM_t *vm;
  ITEMSTORE_t *itemstore;
  uv_loop_t *loop;
  const char *itemstore_filename;
  ITEMSTORE_DURABILITY_e itemstore_durability;
  const char *srcroot;
  const char *input_name;
  const char *inputline_name;
  const char *inputtext_name;
  NetworkRuntime *network;
  bool *safe_shutdown;
  bool *shutdown_requested;
  bool strict_runtime_contracts;
  /* Zero uses the compiler default; tests may lower this per invocation. */
  size_t compiler_ast_node_limit;
  volatile sig_atomic_t *interrupt_pending;
  bool *signal_shutdown_requested;

  // Owned by this RuntimeContext invocation and freely mutated while the
  // interpreter runs. The bytecode and ITEM_t objects referenced by these
  // fields remain borrowed from the itemstore or caller.
  RuntimeDecoder decoder;
  ITEM_t *current_item;
  ITEM_t *pending_call_item;
  /* Number of execution pins owned by the active frame invocation. */
  size_t frame_owned_count;
  // Borrowed caller boundary for the active interpret() invocation. Nested
  // invocations save and restore both fields.
  int invocation_callstack_floor;
  ITEM_t *invocation_caller_item;
  OP_t opcode[256];
  LibcallRegistry *libcalls;
  bool initialized;
  bool interrupted;
  // Zero when the context is not inside a task callback, otherwise the
  // positive integer id of the timer-backed task whose callback is active.
  uint64_t current_task_id;
  RuntimeVerifyCacheEntry runtime_verify_cache[RUNTIME_VERIFY_CACHE_SIZE];
  size_t runtime_verify_cache_next;
  ITEMSTORE_t *runtime_verify_cache_owner;
  uint64_t runtime_verify_cache_topology_revision;
  uint64_t runtime_verify_cache_topology_epoch;
  uint64_t runtime_verify_cache_revision;
  uint64_t runtime_verify_cache_revision_epoch;
  bool runtime_verify_cache_revision_valid;
  /* Test-only diagnostic: number of actual verifier invocations. */
  uint64_t verifier_invocations_for_tests;
};

bool runtime_init(RuntimeContext *ctx, VM_t *vm);
void runtime_destroy(RuntimeContext *ctx);
void runtime_context_init(RuntimeContext *ctx, VM_t *vm);
uint64_t runtime_verify_invocations_for_tests(const RuntimeContext *ctx);
void runtime_verify_invocations_reset_for_tests(RuntimeContext *ctx);
void runtime_verify_cache_clear_for_tests(RuntimeContext *ctx);
