#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "error.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "list.h"
#include "stack.h"
#include "test_assert.h"

#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static bool force_gmtime_r_failure;

extern struct tm *__real_gmtime_r(const time_t *timep, struct tm *result);

struct tm *__wrap_gmtime_r(const time_t *timep, struct tm *result) {
  if (force_gmtime_r_failure) return NULL;
  return __real_gmtime_r(timep, result);
}

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

static VALUE_t call_make(VALUE_t year, VALUE_t month, VALUE_t day,
                         VALUE_t hour, VALUE_t minute, VALUE_t second,
                         uint8_t *sentinel) {
  push_stack(config.vm->stack, year);
  push_stack(config.vm->stack, month);
  push_stack(config.vm->stack, day);
  push_stack(config.vm->stack, hour);
  push_stack(config.vm->stack, minute);
  push_stack(config.vm->stack, second);
  uint8_t *nextop = lc_time_make(test_ctx(), sentinel, NULL);
  ASSERT_TRUE(nextop == sentinel);
  return pop_stack(config.vm->stack);
}

void test_time_year_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;
  size_t count = 0;

  while (libcalls[count].libname != NULL) count++;
  ASSERT_EQ_INT(110, count);
  ASSERT_TRUE(libcall_lookup_pair("time", "year", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(8, lib_index);
  ASSERT_EQ_INT(0, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_time_year);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);

  const char *names[] = {"month", "day", "hour", "minute", "second",
                         "timestamp", "time", "date", "fulldate", "weekday",
                         "make"};
  TimeHandler handlers[] = {lc_time_month, lc_time_day, lc_time_hour,
                            lc_time_minute, lc_time_second, lc_time_timestamp,
                            lc_time_time, lc_time_date, lc_time_fulldate,
                            lc_time_weekday, lc_time_make};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    ASSERT_TRUE(libcall_lookup_pair("time", names[i], &lib_index, &call_index,
                                   &args));
    ASSERT_EQ_INT(8, lib_index);
    ASSERT_EQ_INT((int)i + 1, call_index);
    ASSERT_EQ_INT(strcmp(names[i], "make") == 0 ? 6 : 1, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == handlers[i]);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(strcmp(names[i], "make") == 0 ? 6 : 1, args);
  }
}

void test_time_make_utc_calendar_and_epoch_vectors(void) {
  static const struct {
    int64_t year, month, day, hour, minute, second, expected;
  } cases[] = {
      {1970, 1, 1, 0, 0, 0, INT64_C(0)},
      {2026, 9, 23, 12, 30, 0, INT64_C(1790166600000)},
      {1969, 12, 31, 23, 59, 59, INT64_C(-1000)},
      {1900, 2, 28, 23, 59, 59, INT64_C(-2203891201000)},
      {2000, 2, 29, 0, 0, 0, INT64_C(951782400000)},
      {2100, 2, 28, 0, 0, 0, INT64_C(4107456000000)},
      {2400, 2, 29, 0, 0, 0, INT64_C(13574563200000)},
      {0, 1, 1, 0, 0, 0, INT64_C(-62167219200000)},
      {-1, 1, 1, 0, 0, 0, INT64_C(-62198755200000)},
      {-4, 2, 29, 0, 0, 0, INT64_C(-62288352000000)},
      {2020, 1, 31, 0, 0, 0, INT64_C(1580428800000)},
      {2020, 2, 29, 0, 0, 0, INT64_C(1582934400000)},
      {2020, 3, 31, 0, 0, 0, INT64_C(1585612800000)},
      {2020, 4, 30, 0, 0, 0, INT64_C(1588204800000)},
      {2020, 5, 31, 0, 0, 0, INT64_C(1590883200000)},
      {2020, 6, 30, 0, 0, 0, INT64_C(1593475200000)},
      {2020, 7, 31, 0, 0, 0, INT64_C(1596153600000)},
      {2020, 8, 31, 0, 0, 0, INT64_C(1598832000000)},
      {2020, 9, 30, 0, 0, 0, INT64_C(1601424000000)},
      {2020, 10, 31, 0, 0, 0, INT64_C(1604102400000)},
      {2020, 11, 30, 0, 0, 0, INT64_C(1606694400000)},
      {2020, 12, 31, 0, 0, 0, INT64_C(1609372800000)},
  };
  const char *previous = getenv("TZ");
  bool had_previous = previous != NULL;
  char *saved = previous ? strdup(previous) : NULL;
  ASSERT_TRUE(!previous || saved != NULL);
  ASSERT_EQ_INT(0, setenv("TZ", "UTC+8", 1));
  tzset();

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    uint8_t sentinel = 0xa5;
    VALUE_t result = call_make(
        (VALUE_t){VALUE_int, {.i = cases[i].year}},
        (VALUE_t){VALUE_int, {.i = cases[i].month}},
        (VALUE_t){VALUE_int, {.i = cases[i].day}},
        (VALUE_t){VALUE_int, {.i = cases[i].hour}},
        (VALUE_t){VALUE_int, {.i = cases[i].minute}},
        (VALUE_t){VALUE_int, {.i = cases[i].second}}, &sentinel);
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(cases[i].expected, result.i);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();

  if (had_previous) {
    ASSERT_EQ_INT(0, setenv("TZ", saved, 1));
  } else {
    ASSERT_EQ_INT(0, unsetenv("TZ"));
  }
  tzset();
  free(saved);
}

void test_time_make_validates_all_components_and_consumes_owned_values(void) {
  static const int64_t valid[] = {2026, 9, 23, 12, 30, 0};
  static const VALUE_e invalid_types[] = {
      VALUE_float, VALUE_str, VALUE_bool, VALUE_nil, VALUE_list,
  };

  setup_libcall_runtime();
  for (size_t position = 0; position < 6; ++position) {
    for (size_t kind = 0; kind < sizeof(invalid_types) /
        sizeof(invalid_types[0]); ++kind) {
      VALUE_t values[6];
      for (size_t i = 0; i < 6; ++i) {
        values[i] = (VALUE_t){VALUE_int, {.i = valid[i]}};
      }
      switch (invalid_types[kind]) {
        case VALUE_float:
          values[position] = (VALUE_t){VALUE_float, {.f = 1.0}};
          break;
        case VALUE_str:
          values[position] = (VALUE_t){VALUE_str, {.s = strdup("bad")}};
          ASSERT_NOT_NULL(values[position].s);
          break;
        case VALUE_bool:
          values[position] = (VALUE_t){VALUE_bool, {.i = 1}};
          break;
        case VALUE_nil:
          values[position] = VALUE_NIL;
          break;
        case VALUE_list:
          values[position] = (VALUE_t){VALUE_list,
              {.list = sin_list_build_owned(NULL, 0)}};
          ASSERT_NOT_NULL(values[position].list);
          break;
        default:
          ASSERT_TRUE(false);
      }
      VALUE_t result = call_make(values[0], values[1], values[2], values[3],
                                 values[4], values[5], NULL);
      ASSERT_EQ_INT(VALUE_nil, result.type);
      assert_invalid_args_detail_contains("time.make");
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    }
  }

  VALUE_t first = {VALUE_str, {.s = strdup("bad year")}};
  VALUE_t second = {VALUE_str, {.s = strdup("bad month")}};
  ASSERT_NOT_NULL(first.s);
  ASSERT_NOT_NULL(second.s);
  VALUE_t result = call_make(first, second,
      (VALUE_t){VALUE_int, {.i = 23}}, (VALUE_t){VALUE_int, {.i = 12}},
      (VALUE_t){VALUE_int, {.i = 30}}, (VALUE_t){VALUE_int, {.i = 0}},
      NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("time.make");
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  static const struct {
    int64_t year, month, day, hour, minute, second;
  } invalid_calendar[] = {
      {2026, 0, 23, 12, 30, 0}, {2026, 13, 23, 12, 30, 0},
      {2026, 2, 29, 12, 30, 0}, {1900, 2, 29, 12, 30, 0},
      {2026, 4, 31, 12, 30, 0}, {2026, 9, 0, 12, 30, 0},
      {2026, 9, 23, -1, 30, 0}, {2026, 9, 23, 24, 30, 0},
      {2026, 9, 23, 12, -1, 0}, {2026, 9, 23, 12, 60, 0},
      {2026, 9, 23, 12, 30, -1}, {2026, 9, 23, 12, 30, 60},
  };
  for (size_t i = 0; i < sizeof(invalid_calendar) /
      sizeof(invalid_calendar[0]); ++i) {
    result = call_make(
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].year}},
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].month}},
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].day}},
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].hour}},
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].minute}},
        (VALUE_t){VALUE_int, {.i = invalid_calendar[i].second}}, NULL);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("time.make");
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_make_overflow_error_priority_and_preserved_error(void) {
  static const int64_t min_year = -292275055;
  static const int64_t max_year = 292278994;
  setup_libcall_runtime();

  VALUE_t result = call_make(
      (VALUE_t){VALUE_int, {.i = min_year}},
      (VALUE_t){VALUE_int, {.i = 5}}, (VALUE_t){VALUE_int, {.i = 16}},
      (VALUE_t){VALUE_int, {.i = 16}}, (VALUE_t){VALUE_int, {.i = 47}},
      (VALUE_t){VALUE_int, {.i = 5}}, NULL);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(INT64_C(-9223372036854775000), result.i);
  result = call_make(
      (VALUE_t){VALUE_int, {.i = min_year}},
      (VALUE_t){VALUE_int, {.i = 5}}, (VALUE_t){VALUE_int, {.i = 16}},
      (VALUE_t){VALUE_int, {.i = 16}}, (VALUE_t){VALUE_int, {.i = 47}},
      (VALUE_t){VALUE_int, {.i = 4}}, NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);

  result = call_make(
      (VALUE_t){VALUE_int, {.i = max_year}},
      (VALUE_t){VALUE_int, {.i = 8}}, (VALUE_t){VALUE_int, {.i = 17}},
      (VALUE_t){VALUE_int, {.i = 7}}, (VALUE_t){VALUE_int, {.i = 12}},
      (VALUE_t){VALUE_int, {.i = 55}}, NULL);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(INT64_C(9223372036854775000), result.i);
  result = call_make(
      (VALUE_t){VALUE_int, {.i = max_year}},
      (VALUE_t){VALUE_int, {.i = 8}}, (VALUE_t){VALUE_int, {.i = 17}},
      (VALUE_t){VALUE_int, {.i = 7}}, (VALUE_t){VALUE_int, {.i = 12}},
      (VALUE_t){VALUE_int, {.i = 56}}, NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);

  result = call_make(
      (VALUE_t){VALUE_int, {.i = INT64_MAX}},
      (VALUE_t){VALUE_int, {.i = 0}}, (VALUE_t){VALUE_int, {.i = 0}},
      (VALUE_t){VALUE_int, {.i = 0}}, (VALUE_t){VALUE_int, {.i = 0}},
      (VALUE_t){VALUE_int, {.i = 0}}, NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  assert_invalid_args_detail_contains("time.make");

  result = call_make(
      (VALUE_t){VALUE_int, {.i = INT64_MAX}},
      (VALUE_t){VALUE_int, {.i = 1}}, (VALUE_t){VALUE_int, {.i = 1}},
      (VALUE_t){VALUE_int, {.i = 0}}, (VALUE_t){VALUE_int, {.i = 0}},
      (VALUE_t){VALUE_int, {.i = 0}}, NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);

  result = call_make(
      (VALUE_t){VALUE_int, {.i = INT64_MIN}},
      (VALUE_t){VALUE_int, {.i = 1}}, (VALUE_t){VALUE_int, {.i = 1}},
      (VALUE_t){VALUE_int, {.i = 0}}, (VALUE_t){VALUE_int, {.i = 0}},
      (VALUE_t){VALUE_int, {.i = 0}}, NULL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  result = call_make(
      (VALUE_t){VALUE_int, {.i = 1970}}, (VALUE_t){VALUE_int, {.i = 1}},
      (VALUE_t){VALUE_int, {.i = 1}}, (VALUE_t){VALUE_int, {.i = 0}},
      (VALUE_t){VALUE_int, {.i = 0}}, (VALUE_t){VALUE_int, {.i = 0}}, NULL);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior error") != NULL);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_make_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.made = time.make{2026, 9, 23, 12, 30, 0};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);
  ITEM_t *made = find_item(itemstore_root(config.itemstore_ctx), "result.made");
  ASSERT_NOT_NULL(made);
  ASSERT_EQ_INT(VALUE_int, item_value(made)->type);
  ASSERT_EQ_INT(INT64_C(1790166600000), item_value(made)->i);

  const char *invalid[] = {"time.make;", "time.make{1, 2, 3, 4, 5};",
                           "time.make{1, 2, 3, 4, 5, 6, 7};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
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

void test_time_weekday_utc_all_days_and_boundaries(void) {
  static const int64_t midnight_timestamps[] = {
      INT64_C(259200000), INT64_C(345600000), INT64_C(432000000),
      INT64_C(518400000), INT64_C(604800000), INT64_C(691200000),
      INT64_C(777600000),
  };
  static const int64_t expected[] = {7, 1, 2, 3, 4, 5, 6};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(midnight_timestamps) /
      sizeof(midnight_timestamps[0]); i++) {
    VALUE_t result = call_time(lc_time_weekday,
        (VALUE_t){VALUE_int, {.i = midnight_timestamps[i]}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(expected[i], result.i);
  }

  VALUE_t result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = INT64_C(259199999)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(6, result.i);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = INT64_C(345599999)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = -1}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(3, result.i);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = INT64_C(-345600000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = INT64_C(-259200001)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = INT64_C(-259200000)}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_weekday_is_utc_under_nonutc_timezone(void) {
  const char *previous = getenv("TZ");
  bool had_previous = previous != NULL;
  char *saved = previous ? strdup(previous) : NULL;
  ASSERT_TRUE(!previous || saved != NULL);
  ASSERT_EQ_INT(0, setenv("TZ", "UTC+8", 1));
  tzset();

  setup_libcall_runtime();
  VALUE_t result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(4, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();

  if (had_previous) {
    ASSERT_EQ_INT(0, setenv("TZ", saved, 1));
  } else {
    ASSERT_EQ_INT(0, unsetenv("TZ"));
  }
  tzset();
  ASSERT_TRUE((getenv("TZ") != NULL) == had_previous);
  if (had_previous) ASSERT_TRUE(strcmp(getenv("TZ"), saved) == 0);
  free(saved);
}

void test_time_weekday_rejects_invalid_types_and_preserves_error(void) {
  setup_libcall_runtime();
  VALUE_t invalid_string = {VALUE_str, {.s = strdup("not milliseconds")}};
  ASSERT_NOT_NULL(invalid_string.s);
  VALUE_t result = call_time(lc_time_weekday, invalid_string);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("time.weekday");

  VALUE_t invalids[] = {
      {VALUE_float, {.f = 1.5}},
      {VALUE_bool, {.i = 1}},
      VALUE_NIL,
      {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}},
      {VALUE_float, {.f = 0.0}},
  };
  ASSERT_NOT_NULL(invalids[3].list);
  for (size_t i = 0; i < sizeof(invalids) / sizeof(invalids[0]); i++) {
    result = call_time(lc_time_weekday, invalids[i]);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("time.weekday");
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(4, result.i);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strcmp(item_value(message)->s,
                    "Invalid arguments to library call. (prior error)") == 0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_weekday_conversion_failure_is_undefined(void) {
  setup_libcall_runtime();
  force_gmtime_r_failure = true;
  VALUE_t result = call_time(lc_time_weekday,
      (VALUE_t){VALUE_int, {.i = 0}});
  force_gmtime_r_failure = false;
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_weekday_extreme_timestamp_follows_host_support(void) {
  setup_libcall_runtime();
  const int64_t extremes[] = {INT64_MIN, INT64_MAX};
  for (size_t i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
    VALUE_t result = call_time(lc_time_weekday,
        (VALUE_t){VALUE_int, {.i = extremes[i]}});
    if (result.type == VALUE_int) {
      ASSERT_TRUE(result.i >= 1 && result.i <= 7);
    } else {
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
      ASSERT_NOT_NULL(error);
      ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
    }
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_weekday_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.weekday = time.weekday{0};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  VALUE_t result;
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);
  ITEM_t *weekday = find_item(itemstore_root(config.itemstore_ctx),
                              "result.weekday");
  ASSERT_NOT_NULL(weekday);
  ASSERT_EQ_INT(VALUE_int, item_value(weekday)->type);
  ASSERT_EQ_INT(4, item_value(weekday)->i);

  const char *invalid[] = {"time.weekday;", "time.weekday{1, 2};"};
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

void test_time_formatted_utc_boundaries_and_negative_flooring(void) {
  static const TimeHandler handlers[] = {lc_time_timestamp, lc_time_time,
      lc_time_date, lc_time_fulldate};
  static const char *const epoch[] = {"1970-01-01 00:00:00", "00:00:00",
                                      "1970-01-01", "1st January 1970"};
  static const char *const leap[] = {"2020-02-29 12:34:56", "12:34:56",
                                     "2020-02-29", "29th February 2020"};
  static const char *const negative[] = {"1969-12-31 23:59:59", "23:59:59",
                                         "1969-12-31", "31st December 1969"};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    VALUE_t result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = 0}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, epoch[i]) == 0);
    value_free(&result);
    result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = INT64_C(1582979696000)}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, leap[i]) == 0);
    value_free(&result);
    result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = -1}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, negative[i]) == 0);
    value_free(&result);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_formatted_year_boundaries(void) {
  static const TimeHandler handlers[] = {lc_time_timestamp, lc_time_date,
                                         lc_time_fulldate};
  static const char *const year_zero[] = {"0000-01-01 00:00:00",
                                          "0000-01-01",
                                          "1st January 0000"};
  static const char *const year_9999[] = {"9999-12-31 00:00:00",
                                          "9999-12-31",
                                          "31st December 9999"};
  static const int64_t supported_timestamps[] = {
      INT64_C(-62167219200000), INT64_C(253402214400000)};
  static const int64_t out_of_range_timestamps[] = {
      INT64_C(-62198755200000), INT64_C(253402300800000)};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    VALUE_t result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = supported_timestamps[0]}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, year_zero[i]) == 0);
    value_free(&result);

    result = call_time(handlers[i],
        (VALUE_t){VALUE_int, {.i = supported_timestamps[1]}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, year_9999[i]) == 0);
    value_free(&result);
  }

  VALUE_t result = call_time(lc_time_time,
      (VALUE_t){VALUE_int, {.i = out_of_range_timestamps[1]}});
  if (result.type == VALUE_str) {
    ASSERT_TRUE(strcmp(result.s, "00:00:00") == 0);
    value_free(&result);
  } else {
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
  }

  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    for (size_t j = 0; j < sizeof(out_of_range_timestamps) /
        sizeof(out_of_range_timestamps[0]); j++) {
      result = call_time(handlers[i],
          (VALUE_t){VALUE_int, {.i = out_of_range_timestamps[j]}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
      ASSERT_NOT_NULL(error);
      ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
    }
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_fulldate_ordinal_suffixes_and_month_names(void) {
  static const int64_t timestamps[] = {
      INT64_C(1577836800000), INT64_C(1580601600000),
      INT64_C(1583193600000), INT64_C(1585958400000),
      INT64_C(1589155200000), INT64_C(1591920000000),
      INT64_C(1594598400000), INT64_C(1597968000000),
      INT64_C(1600732800000), INT64_C(1603411200000),
      INT64_C(1604448000000), INT64_C(1609372800000),
  };
  static const char *const expected[] = {
      "1st January 2020", "2nd February 2020", "3rd March 2020",
      "4th April 2020", "11th May 2020", "12th June 2020",
      "13th July 2020", "21st August 2020", "22nd September 2020",
      "23rd October 2020", "4th November 2020", "31st December 2020",
  };

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(timestamps) / sizeof(timestamps[0]); i++) {
    VALUE_t result = call_time(lc_time_fulldate,
        (VALUE_t){VALUE_int, {.i = timestamps[i]}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, expected[i]) == 0);
    value_free(&result);
  }
  const int64_t ordinal_timestamps[] = {INT64_C(1580601600000),
      INT64_C(1583193600000), INT64_C(1585958400000), INT64_C(1589155200000),
      INT64_C(1591920000000), INT64_C(1594598400000), INT64_C(1597968000000),
      INT64_C(1600732800000), INT64_C(1603411200000), INT64_C(1609372800000)};
  const char *ordinal_expected[] = {"2nd February 2020", "3rd March 2020",
      "4th April 2020", "11th May 2020", "12th June 2020",
      "13th July 2020", "21st August 2020", "22nd September 2020",
      "23rd October 2020", "31st December 2020"};
  for (size_t i = 0; i < sizeof(ordinal_timestamps) /
      sizeof(ordinal_timestamps[0]); i++) {
    VALUE_t result = call_time(lc_time_fulldate,
        (VALUE_t){VALUE_int, {.i = ordinal_timestamps[i]}});
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_TRUE(strcmp(result.s, ordinal_expected[i]) == 0);
    value_free(&result);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_formatted_rejects_invalid_types_and_publishes_details(void) {
  static const TimeHandler handlers[] = {lc_time_timestamp, lc_time_time,
      lc_time_date, lc_time_fulldate};
  static const char *const names[] = {"time.timestamp", "time.time",
                                      "time.date", "time.fulldate"};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    VALUE_t invalid = {VALUE_str, {.s = strdup("not milliseconds")}};
    ASSERT_NOT_NULL(invalid.s);
    VALUE_t result = call_time(handlers[i], invalid);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains(names[i]);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_formatted_unrepresentable_timestamp_publishes_error(void) {
  setup_libcall_runtime();
  VALUE_t result = call_time(lc_time_timestamp,
      (VALUE_t){VALUE_int, {.i = INT64_MAX}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_formatted_success_preserves_existing_error(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  VALUE_t result = call_time(lc_time_timestamp,
      (VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "1970-01-01 00:00:00") == 0);
  value_free(&result);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strcmp(item_value(message)->s,
                    "Invalid arguments to library call. (prior error)") == 0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_time_formatted_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.timestamp = time.timestamp{-1};"
      "result.time = time.time{1582979696789};"
      "result.date = time.date{1582979696789};"
      "result.fulldate = time.fulldate{1582979696789};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);

  static const char *const names[] = {"timestamp", "time", "date",
                                      "fulldate"};
  static const char *const expected[] = {"1969-12-31 23:59:59", "12:34:56",
                                         "2020-02-29", "29th February 2020"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    char path[32];
    ASSERT_TRUE(snprintf(path, sizeof(path), "result.%s", names[i]) > 0);
    ITEM_t *value = find_item(itemstore_root(config.itemstore_ctx), path);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ_INT(VALUE_str, item_value(value)->type);
    ASSERT_TRUE(strcmp(item_value(value)->s, expected[i]) == 0);
  }

  const char *invalid[] = {"time.timestamp;", "time.timestamp{1, 2};",
      "time.time;", "time.time{1, 2};", "time.date;", "time.date{1, 2};",
      "time.fulldate;", "time.fulldate{1, 2};"};
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
