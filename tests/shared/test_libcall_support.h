#ifndef TEST_LIBCALL_SUPPORT_H
#define TEST_LIBCALL_SUPPORT_H

#include "interpret.h"
#include "item.h"
#include "value.h"
#include "network.h"
#include "test_network_fixture.h"

RuntimeContext *test_ctx(void);
void setup_libcall_runtime(void);
void teardown_libcall_runtime(void);
void assert_invalid_args_detail_contains(const char *expected);
void assert_invalid_args_float_detail_contains(const char *expected);
void assert_bool_return(VALUE_t value, int expected);
NetworkRuntime *test_network_runtime(void);
bool test_network_reset(size_t maxconns);
void test_network_clear(void);
void test_network_drain(void);

#endif
