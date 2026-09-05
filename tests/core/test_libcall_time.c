#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "error.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "stack.h"
#include "test_assert.h"

#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static VALUE_t call_year(VALUE_t input) {
  push_stack(config.vm->stack, input);
  (void)lc_time_year(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

typedef uint8_t *(*TimeHandler)(RuntimeContext *, uint8_t *, ITEM_t *);

static VALUE_t call_time(TimeHandler handler, VALUE_t input) {
  push_stack(config.vm->stack, input);
  (void)handler(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

void test_time_year_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;
  size_t count = 0;

  while (libcalls[count].libname != NULL) count++;
  ASSERT_EQ_INT(92, count);
  ASSERT_TRUE(libcall_lookup_pair("time", "year", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(8, lib_index);
  ASSERT_EQ_INT(0, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_time_year);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);

  const char *names[] = {"month", "day", "hour", "minute", "second"};
  TimeHandler handlers[] = {lc_time_month, lc_time_day, lc_time_hour,
                            lc_time_minute, lc_time_second};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    ASSERT_TRUE(libcall_lookup_pair("time", names[i], &lib_index, &call_index,
                                   &args));
    ASSERT_EQ_INT(8, lib_index);
    ASSERT_EQ_INT((int)i + 1, call_index);
    ASSERT_EQ_INT(1, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == handlers[i]);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(1, args);
  }
}

void test_time_year_utc_calendar_boundaries(void) {
  setup_libcall_runtime();
  VALUE_t result = call_year((VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1970, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = INT64_C(946684800000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(2000, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = INT64_C(951782400000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(2000, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = INT64_C(951868800000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(2000, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = INT64_C(-86400000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1969, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_year_negative_millisecond_flooring(void) {
  setup_libcall_runtime();
  VALUE_t result = call_year((VALUE_t){VALUE_int, {.i = -1}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1969, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = -1001}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1969, result.i);
  result = call_year((VALUE_t){VALUE_int, {.i = INT64_C(-2208988800000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1900, result.i);
  teardown_libcall_runtime();
}

void test_time_calendar_components_are_utc_integers(void) {
  static const TimeHandler handlers[] = {lc_time_month, lc_time_day,
      lc_time_hour, lc_time_minute, lc_time_second};
  static const int64_t expected[] = {2, 29, 12, 34, 56};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    VALUE_t result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = INT64_C(1582979696789)}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(expected[i], result.i);
  }
  VALUE_t result = call_time(lc_time_second,
      (VALUE_t){VALUE_int, {.i = -1}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(59, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_year_rejects_invalid_type_and_publishes_error(void) {
  setup_libcall_runtime();
  VALUE_t invalid = {VALUE_str, {.s = strdup("not milliseconds")}};
  ASSERT_NOT_NULL(invalid.s);
  VALUE_t result = call_year(invalid);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("time.year");
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_year_unrepresentable_timestamp_publishes_error(void) {
  setup_libcall_runtime();
  VALUE_t result = call_year((VALUE_t){VALUE_int, {.i = INT64_MAX}});
  if (result.type == VALUE_nil) {
    ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
  } else {
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_TRUE(result.i > INT64_C(100000000));
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_year_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup("result.year = time.year{-1};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  VALUE_t result;
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);
  ITEM_t *year = find_item(itemstore_root(config.itemstore_ctx), "result.year");
  ASSERT_NOT_NULL(year);
  ASSERT_EQ_INT(VALUE_int, item_value(year)->type);
  ASSERT_EQ_INT(1969, item_value(year)->i);

  const char *invalid[] = {"time.year;", "time.year{1, 2};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}

void test_time_calendar_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.month = time.month{1582979696789};"
      "result.day = time.day{1582979696789};"
      "result.hour = time.hour{1582979696789};"
      "result.minute = time.minute{1582979696789};"
      "result.second = time.second{1582979696789};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);

  const char *names[] = {"month", "day", "hour", "minute", "second"};
  const int64_t expected[] = {2, 29, 12, 34, 56};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    char path[32];
    ASSERT_TRUE(snprintf(path, sizeof(path), "result.%s", names[i]) > 0);
    ITEM_t *value = find_item(itemstore_root(config.itemstore_ctx), path);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ_INT(VALUE_int, item_value(value)->type);
    ASSERT_EQ_INT(expected[i], item_value(value)->i);
  }

  const char *invalid[] = {"time.month;", "time.day{1, 2};",
      "time.hour;", "time.minute{1, 2};", "time.second;"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}
