#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Process-wide, runtime-thread-confined state; initialization is idempotent. */
bool libcall_rand_init(void);

/* C-only test hooks. Install/reset only while the process is quiescent and
 * run tests serially. NULL restores OS entropy or the real generator. Reset
 * discards the seed and draw override; it is never used by runtime teardown. */
void libcall_rand_test_reset(bool (*entropy)(uint64_t seed[4]));
void libcall_rand_test_draw(uint64_t (*draw)(void));
