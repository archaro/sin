#include "test_framework.h"

void test_stack_reset_to_frees_values_at_boundaries(void);
void test_transactional_frame_entry_rejects_stack_and_callstack_overflow(void);
void test_runtime_frame_direct_lifecycle_restores_state(void);
void test_runtime_frame_normalizes_more_than_255_arguments(void);
void test_runtime_frame_failure_ownership_and_return_capacity(void);
void test_runtime_frame_nested_invocation_preserves_pending_transfer(void);
void test_large_local_direct_and_sys_call_rejection_reuses_vm(void);
void test_nested_string_frames_release_locals_and_preserve_result(void);
void test_nested_nil_return_releases_frame_locals(void);
void test_top_level_string_frame_cleanup_and_vm_reuse(void);
void test_deferred_interrupt_unwinds_nested_call_frames(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_stack_reset_to_frees_values_at_boundaries", test_stack_reset_to_frees_values_at_boundaries, "exclusive", 30000,
     "api.runtime.stack"},
    {"rewrite.core.test_transactional_frame_entry_rejects_stack_and_callstack_overflow", test_transactional_frame_entry_rejects_stack_and_callstack_overflow, "exclusive", 30000,
     "api.runtime.runtime-frame,api.runtime.stack"},
    {"rewrite.core.test_runtime_frame_direct_lifecycle_restores_state", test_runtime_frame_direct_lifecycle_restores_state, "exclusive", 30000,
     "language.expression.call,language.expression.libcall,api.runtime.runtime-frame,api.runtime.vm"},
    {"rewrite.core.test_runtime_frame_normalizes_more_than_255_arguments", test_runtime_frame_normalizes_more_than_255_arguments, "exclusive", 30000,
     "api.runtime.runtime-frame"},
    {"rewrite.core.test_runtime_frame_failure_ownership_and_return_capacity", test_runtime_frame_failure_ownership_and_return_capacity, "exclusive", 30000,
     "api.runtime.opcode-handlers,api.runtime.runtime-frame,api.runtime.vm"},
    {"rewrite.core.test_runtime_frame_nested_invocation_preserves_pending_transfer", test_runtime_frame_nested_invocation_preserves_pending_transfer, "exclusive", 30000,
     "language.expression.call,api.runtime.runtime-frame"},
    {"rewrite.core.test_large_local_direct_and_sys_call_rejection_reuses_vm", test_large_local_direct_and_sys_call_rejection_reuses_vm, "exclusive", 30000,
     "api.runtime.runtime-frame"},
    {"rewrite.core.test_nested_string_frames_release_locals_and_preserve_result", test_nested_string_frames_release_locals_and_preserve_result, "exclusive", 30000,
     "api.runtime.runtime-frame"},
    {"rewrite.core.test_nested_nil_return_releases_frame_locals", test_nested_nil_return_releases_frame_locals, "exclusive", 30000,
     "api.runtime.runtime-frame"},
    {"rewrite.core.test_top_level_string_frame_cleanup_and_vm_reuse", test_top_level_string_frame_cleanup_and_vm_reuse, "exclusive", 30000,
     "api.runtime.runtime-frame"},
    {"rewrite.core.test_deferred_interrupt_unwinds_nested_call_frames", test_deferred_interrupt_unwinds_nested_call_frames, "exclusive", 30000,
     "api.runtime.runtime-frame"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
