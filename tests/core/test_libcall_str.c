#include "item.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glob.h>
#include <unistd.h>
#include <stdint.h>

#include "libcall.h"
#include "config.h"
#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "task.h"
#include "vm.h"
#include "memory.h"
#include "runtime_value.h"
#include "string_limits.h"
#include "version.h"

#include "network.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_thisid(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_count(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
void execute_task_cb(uv_timer_t *req);
uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_echo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_maxlines(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_connected(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_address(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_save(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_thisitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_parentitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_itemtype(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_childcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_rootcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_version(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_now(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_monotime(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_calleritem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_paramcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_source(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
int64_t lc_sys_wall_milliseconds(int64_t seconds, int64_t microseconds);
uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_delete(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_nthname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_rootname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_len(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_valtostr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_trim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_ltrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_rtrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_substr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_find(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_contains(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_startswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_endswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_eqcasei(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_replace(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_repeat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_padleft(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_padright(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

extern CONFIG_t config;

#include "shared/test_libcall_support.h"

static void assert_float_string_libcall_returns_invalidargs_nil(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *expected) {
  VALUE_t arg = {VALUE_float, {.f = 1.25}};
  push_stack(config.vm->stack, arg);
  (void)func(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains(expected);
}
static void assert_float_string_libcall_uses_context_itemroot(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *expected) {
  ITEM_t *context_root = make_root_item("context-root");
  ASSERT_NOT_NULL(context_root);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = itemstore_owner(context_root);

  VALUE_t arg = {VALUE_float, {.f = 1.25}};
  push_stack(config.vm->stack, arg);
  (void)func(&ctx, NULL, context_root);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  ITEM_t *context_err = find_item(context_root, "error");
  ASSERT_NOT_NULL(context_err);
  ASSERT_EQ_INT(VALUE_int, item_value(context_err)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(context_err)->i);
  ITEM_t *context_msg = find_item(context_root, "error.msg");
  ASSERT_NOT_NULL(context_msg);
  ASSERT_EQ_INT(VALUE_str, item_value(context_msg)->type);
  ASSERT_TRUE(strstr(item_value(context_msg)->s, expected) != NULL);

  ITEM_t *global_err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_TRUE(global_err == NULL || item_value(global_err)->type == VALUE_nil);

  destroy_item(context_root);
}

static void assert_str_unary_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *input,
    const char *expected) {
  VALUE_t text = {VALUE_str, {.s = strdup(input)}};
  push_stack(config.vm->stack, text);
  (void)func(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_substr_result(const char *input, int64_t start,
                                     int64_t len, const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(input)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = start}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = len}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_valtostr_result(VALUE_t input, const char *expected) {
  push_stack(config.vm->stack, input);
  (void)lc_str_valtostr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_replace_result(const char *text, const char *old_text,
                                      const char *new_text,
                                      const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(old_text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(new_text)}});
  (void)lc_str_replace(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_repeat_result(const char *text, int64_t count,
                                     const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = count}});
  (void)lc_str_repeat(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_pad_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *text, int64_t width, const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = width}});
  (void)func(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_find_result(const char *haystack, const char *needle,
                                   int64_t expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)lc_str_find(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

static void assert_str_contains_result(const char *haystack, const char *needle,
                                       int expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)lc_str_contains(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

static void assert_str_affix_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *haystack, const char *needle, int expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)func(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

void test_str_libcalls_float_returns_invalidargs_nil(void) {
  setup_libcall_runtime();

  assert_float_string_libcall_returns_invalidargs_nil(lc_str_capitalise,
      "str.capitalise");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_upper,
      "str.upper");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_lower,
      "str.lower");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_len,
      "str.len");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_trim,
      "str.trim");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_ltrim,
      "str.ltrim");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_rtrim,
      "str.rtrim");

  teardown_libcall_runtime();
}

void test_str_len_returns_string_byte_length(void) {
  setup_libcall_runtime();

  VALUE_t text = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, text);
  (void)lc_str_len(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(5, ret.i);

  VALUE_t empty = {VALUE_str, {.s = strdup("")}};
  push_stack(config.vm->stack, empty);
  (void)lc_str_len(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  teardown_libcall_runtime();
}

void test_str_valtostr_converts_values_to_strings(void) {
  setup_libcall_runtime();

  assert_str_valtostr_result((VALUE_t){VALUE_int, {.i = -42}}, "-42");
  assert_str_valtostr_result((VALUE_t){VALUE_float, {.f = 3.5}}, "3.5");
  assert_str_valtostr_result((VALUE_t){VALUE_bool, {.i = 1}}, "true");
  assert_str_valtostr_result((VALUE_t){VALUE_bool, {.i = 0}}, "false");
  assert_str_valtostr_result(VALUE_NIL, "nil");

  char *original = strdup("already text");
  ASSERT_NOT_NULL(original);
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = original}});
  (void)lc_str_valtostr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(ret.s == original);
  ASSERT_TRUE(strcmp(ret.s, "already text") == 0);
  FREE_STR(ret);

  teardown_libcall_runtime();
}

void test_str_case_libcalls_mutate_strings_in_place(void) {
  setup_libcall_runtime();

  assert_str_unary_result(lc_str_capitalise, "hello", "Hello");
  assert_str_unary_result(lc_str_capitalise, "aLREADY", "ALREADY");
  assert_str_unary_result(lc_str_capitalise, "", "");
  assert_str_unary_result(lc_str_upper, "MiXeD 123!", "MIXED 123!");
  assert_str_unary_result(lc_str_lower, "MiXeD 123!", "mixed 123!");

  teardown_libcall_runtime();
}

void test_str_trim_libcalls_return_trimmed_strings(void) {
  setup_libcall_runtime();

  assert_str_unary_result(lc_str_trim, " \t hello world \r\n", "hello world");
  assert_str_unary_result(lc_str_trim, "   \t\n", "");
  assert_str_unary_result(lc_str_trim, "already clean", "already clean");

  assert_str_unary_result(lc_str_ltrim, " \t hello world \r\n",
                          "hello world \r\n");
  assert_str_unary_result(lc_str_ltrim, "   \t\n", "");
  assert_str_unary_result(lc_str_ltrim, "already clean", "already clean");

  assert_str_unary_result(lc_str_rtrim, " \t hello world \r\n",
                          " \t hello world");
  assert_str_unary_result(lc_str_rtrim, "   \t\n", "");
  assert_str_unary_result(lc_str_rtrim, "already clean", "already clean");

  teardown_libcall_runtime();
}

void test_str_substr_returns_requested_byte_range(void) {
  setup_libcall_runtime();

  assert_str_substr_result("abcdef", 0, 3, "abc");
  assert_str_substr_result("abcdef", 2, 3, "cde");
  assert_str_substr_result("abcdef", 4, 99, "ef");
  assert_str_substr_result("abcdef", 6, 2, "");
  assert_str_substr_result("abcdef", 7, 2, "");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}

void test_str_substr_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr start");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.0}});
  (void)lc_str_substr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  teardown_libcall_runtime();
}

void test_str_find_and_contains_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_find_result("abcdef", "abc", 0);
  assert_str_find_result("abcdef", "cd", 2);
  assert_str_find_result("abcdef", "f", 5);
  assert_str_find_result("abcdef", "missing", -1);
  assert_str_find_result("abcdef", "", 0);

  assert_str_contains_result("abcdef", "abc", 1);
  assert_str_contains_result("abcdef", "cd", 1);
  assert_str_contains_result("abcdef", "missing", 0);
  assert_str_contains_result("abcdef", "", 1);

  teardown_libcall_runtime();
}

void test_str_startswith_and_endswith_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_affix_result(lc_str_startswith, "abcdef", "abc", 1);
  assert_str_affix_result(lc_str_startswith, "abcdef", "abC", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "bc", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "abcdefg", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "", 1);

  assert_str_affix_result(lc_str_endswith, "abcdef", "def", 1);
  assert_str_affix_result(lc_str_endswith, "abcdef", "dEf", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "de", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "zabcdef", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "", 1);

  teardown_libcall_runtime();
}

void test_str_eqcasei_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_affix_result(lc_str_eqcasei, "abcdef", "ABCDEF", 1);
  assert_str_affix_result(lc_str_eqcasei, "MiXeD 123!", "mixed 123!", 1);
  assert_str_affix_result(lc_str_eqcasei, "", "", 1);
  assert_str_affix_result(lc_str_eqcasei, "abcdef", "abcdeg", 0);
  assert_str_affix_result(lc_str_eqcasei, "abcdef", "abc", 0);
  assert_str_affix_result(lc_str_eqcasei, "abc", "abcdef", 0);

  teardown_libcall_runtime();
}

void test_str_replace_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_replace_result("one two one", "one", "three", "three two three");
  assert_str_replace_result("aaaa", "aa", "b", "bb");
  assert_str_replace_result("abc", "x", "y", "abc");
  assert_str_replace_result("abc", "", "x", "abc");
  assert_str_replace_result("abc", "b", "", "ac");
  assert_str_replace_result("", "x", "y", "");

  teardown_libcall_runtime();
}

void test_str_repeat_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_repeat_result("ab", 3, "ababab");
  assert_str_repeat_result("ab", 1, "ab");
  assert_str_repeat_result("ab", 0, "");
  assert_str_repeat_result("", 5, "");

  teardown_libcall_runtime();
}

void test_str_padleft_and_padright_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_pad_result(lc_str_padleft, "abc", 5, "  abc");
  assert_str_pad_result(lc_str_padright, "abc", 5, "abc  ");
  assert_str_pad_result(lc_str_padleft, "abc", 3, "abc");
  assert_str_pad_result(lc_str_padright, "abc", 2, "abc");
  assert_str_pad_result(lc_str_padleft, "", 2, "  ");
  assert_str_pad_result(lc_str_padright, "", 2, "  ");

  teardown_libcall_runtime();
}

void test_str_find_and_contains_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_find(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.find");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("haystack")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_find(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.find");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_contains(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.contains");

  teardown_libcall_runtime();
}

void test_str_startswith_and_endswith_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_startswith(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.startswith");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("haystack")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_endswith(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.endswith");

  teardown_libcall_runtime();
}

void test_str_eqcasei_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("right")}});
  (void)lc_str_eqcasei(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.eqcasei");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("left")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_eqcasei(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.eqcasei");

  teardown_libcall_runtime();
}

void test_str_replace_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("old")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("new")}});
  (void)lc_str_replace(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("new")}});
  (void)lc_str_replace(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("old")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_replace(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  teardown_libcall_runtime();
}

void test_str_repeat_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  (void)lc_str_repeat(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 2.0}});
  (void)lc_str_repeat(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_str_repeat(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  teardown_libcall_runtime();
}

void test_str_growth_libcalls_enforce_string_limit(void) {
  setup_libcall_runtime();

  char *text = malloc(40001);
  ASSERT_NOT_NULL(text);
  memset(text, 'a', 40000);
  text[40000] = '\0';
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = text}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("a")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("aa")}});
  (void)lc_str_replace(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("0123456789")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 7000}});
  (void)lc_str_repeat(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)SIN_MAX_STRING_BYTES + 1}});
  (void)lc_str_padleft(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}

void test_str_padleft_and_padright_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 5}});
  (void)lc_str_padleft(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padleft");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 5.0}});
  (void)lc_str_padright(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padright");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_str_padleft(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padleft");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_str_padright(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padright");

  teardown_libcall_runtime();
}

void test_str_libcall_invalidargs_uses_context_itemroot(void) {
  setup_libcall_runtime();

  assert_float_string_libcall_uses_context_itemroot(lc_str_capitalise,
      "str.capitalise");
  assert_float_string_libcall_uses_context_itemroot(lc_str_upper,
      "str.upper");
  assert_float_string_libcall_uses_context_itemroot(lc_str_lower,
      "str.lower");
  assert_float_string_libcall_uses_context_itemroot(lc_str_len,
      "str.len");
  assert_float_string_libcall_uses_context_itemroot(lc_str_trim,
      "str.trim");
  assert_float_string_libcall_uses_context_itemroot(lc_str_ltrim,
      "str.ltrim");
  assert_float_string_libcall_uses_context_itemroot(lc_str_rtrim,
      "str.rtrim");

  ITEM_t *context_root = make_root_item("context-root");
  ASSERT_NOT_NULL(context_root);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = itemstore_owner(context_root);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(&ctx, NULL, context_root);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  ITEM_t *context_msg = find_item(context_root, "error.msg");
  ASSERT_NOT_NULL(context_msg);
  ASSERT_EQ_INT(VALUE_str, item_value(context_msg)->type);
  ASSERT_TRUE(strstr(item_value(context_msg)->s, "str.substr start") != NULL);

  destroy_item(context_root);

  teardown_libcall_runtime();
}
