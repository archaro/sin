#ifndef TEST_LIBCALL_SUPPORT_H
#define TEST_LIBCALL_SUPPORT_H

#include "interpret.h"
#include "item.h"
#include "value.h"

RuntimeContext *test_ctx(void);
void setup_libcall_runtime(void);
void teardown_libcall_runtime(void);
void assert_invalid_args_detail_contains(const char *expected);
void assert_invalid_args_float_detail_contains(const char *expected);
void assert_bool_return(VALUE_t value, int expected);

#endif
