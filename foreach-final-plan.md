After review, the following issues remain with the FOREACH implementation
proposed in foreach-plan.md:

1. High — valid sequential FOREACH loops can fail near the local limit

src/compiler/semant.c:291-299 unconditionally requires room for three new hidden locals before calling sem_add_local(). However, sequential loops reuse the same depth-zero hidden names, as required by foreach-plan.md:156-158.

After an earlier loop has registered those slots, a subsequent sequential loop can be rejected when ctx->count > 252, even though it needs no new hidden locals. This conflicts with the planned “only nesting consumes budget” behavior.

The existing test covers excessive nesting (tests/compiler/test_compiler_diag_pipeline.c:338-365) but not sequential reuse at the local-budget boundary.

2. Medium — required FOREACH fuzz seed is absent

foreach-plan.md:220 requires a FOREACH sample under tests/fuzz/corpus/scomp. That directory contains only five existing corpus files and no FOREACH sample.

3. Medium — end-to-end non-list test does not verify error

The plan explicitly requires a non-list interpreter test asserting that error remains untouched (foreach-plan.md:212-215).

tests/interpreter/test_interpret_semantics_golden.c:402-403 verifies only that the iterator remains nil. The underlying list.islist unit test does preserve and check error (tests/core/test_libcall_list.c:361-398), but the planned end-to-end FOREACH assertion is missing.

