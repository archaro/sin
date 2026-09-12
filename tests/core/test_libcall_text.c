#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "error.h"
#include "itemref.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "list.h"
#include "memory.h"
#include "stack.h"
#include "string_limits.h"
#include "test_assert.h"
#include "test_helpers.h"

#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static VALUE_t call_split(const char *text, const char *separator) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(separator)}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static void assert_split_strings(VALUE_t result, const char *const *expected,
                                 size_t count) {
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT(count, sin_list_count(result.list));
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *element = sin_list_get(result.list, i);
    ASSERT_NOT_NULL(element);
    ASSERT_EQ_INT(VALUE_str, element->type);
    ASSERT_TRUE(strcmp(element->s, expected[i]) == 0);
  }
  value_free(&result);
}

static VALUE_t call_words(const char *text) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  (void)lc_text_words(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static void assert_words_strings(VALUE_t result, const char *const *expected,
                                 size_t count) {
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT(count, sin_list_count(result.list));
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *element = sin_list_get(result.list, i);
    ASSERT_NOT_NULL(element);
    ASSERT_EQ_INT(VALUE_str, element->type);
    ASSERT_TRUE(strcmp(element->s, expected[i]) == 0);
  }
  value_free(&result);
}

static VALUE_t call_lines(const char *text) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  (void)lc_text_lines(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static void assert_lines_strings(VALUE_t result, const char *const *expected,
                                 size_t count) {
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT(count, sin_list_count(result.list));
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *element = sin_list_get(result.list, i);
    ASSERT_NOT_NULL(element);
    ASSERT_EQ_INT(VALUE_str, element->type);
    ASSERT_TRUE(strcmp(element->s, expected[i]) == 0);
  }
  value_free(&result);
}

void test_text_split_registry_contract(void) {
  uint8_t library = 0, call = 0, args = 0;
  ASSERT_TRUE(libcall_lookup_pair("text", "split", &library, &call, &args));
  ASSERT_EQ_INT(10, library);
  ASSERT_EQ_INT(0, call);
  ASSERT_EQ_INT(2, args);
  ASSERT_TRUE(libcall_func_pair(library, call) == lc_text_split);
  ASSERT_TRUE(libcall_pair_arg_count(library, call, &args));
  ASSERT_EQ_INT(2, args);
}

void test_text_split_literal_fields_and_utf8(void) {
  setup_libcall_runtime();
  const char *example[] = {"this", "is", "a", "string"};
  assert_split_strings(call_split("this is a string", " "), example, 4);
  assert_split_strings(call_split("", " "), NULL, 0);
  assert_split_strings(call_split("\n", "\n"), NULL, 0);
  const char *expected[] = {"one", "two", "three"};
  assert_split_strings(call_split("one::two::::three", "::"), expected, 3);
  const char *edges[] = {"left", "middle"};
  assert_split_strings(call_split("::left::::middle::", "::"), edges, 2);
  const char *whitespace[] = {" a ", "b\tc"};
  assert_split_strings(call_split(" a |b\tc", "|"), whitespace, 2);
  const char *partial[] = {"a"};
  assert_split_strings(call_split("aaaa", "aaa"), partial, 1);
  const char *unicode[] = {"caf\xC3\xA9", "\xE6\x9D\xB1"};
  assert_split_strings(call_split("caf\xC3\xA9<->\xE6\x9D\xB1", "<->"),
                       unicode, 2);
  const char *sensitive[] = {"xxAB", "YY", "ZZ"};
  assert_split_strings(call_split("xxABabYYabZZ", "ab"), sensitive, 3);
  VALUE_t surviving = call_split("first|second", "|");
  ASSERT_EQ_INT(VALUE_list, surviving.type);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("mutated")}});
  (void)lc_str_upper(test_ctx(), NULL, NULL);
  VALUE_t upper = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, upper.type);
  FREE_STR(upper);
  ASSERT_TRUE(strcmp(sin_list_get(surviving.list, 0)->s, "first") == 0);
  value_free(&surviving);
  teardown_libcall_runtime();
}

void test_text_split_empty_and_unmatched_behavior(void) {
  setup_libcall_runtime();
  const char *singleton[] = {"original"};
  assert_split_strings(call_split("original", ""), singleton, 1);
  const char *empty_singleton[] = {""};
  assert_split_strings(call_split("", ""), empty_singleton, 1);
  assert_split_strings(call_split("", ","), NULL, 0);
  const char *unmatched[] = {"unchanged"};
  assert_split_strings(call_split("unchanged", "missing"), unmatched, 1);
  teardown_libcall_runtime();
}

void test_text_split_invalid_types_and_stack_contract(void) {
  setup_libcall_runtime();
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 77}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(",")}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(1, size_stack(config.vm->stack));
  ASSERT_EQ_INT(77, pop_stack(config.vm->stack).i);
  assert_invalid_args_detail_contains("text.split");

  VALUE_t invalid_first[] = {
      {VALUE_nil, {.i = 0}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}},
      {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}}};
  VALUE_t invalid_separator[] = {
      {VALUE_nil, {.i = 0}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}},
      {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}}};
  for (size_t i = 0; i < sizeof(invalid_first) / sizeof(invalid_first[0]); ++i) {
    push_stack(config.vm->stack, invalid_first[i]);
    invalid_first[i] = VALUE_NIL;
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("separator")}});
    (void)lc_text_split(test_ctx(), NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("text.split");
  }
  for (size_t i = 0; i < sizeof(invalid_separator) / sizeof(invalid_separator[0]); ++i) {
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("text")}});
    push_stack(config.vm->stack, invalid_separator[i]);
    invalid_separator[i] = VALUE_NIL;
    (void)lc_text_split(test_ctx(), NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("text.split");
  }

  SIN_LIST_t *list = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(list);
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("x")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_list, {.list = list}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.split");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = NULL}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(",")}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.split");

  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = NULL}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.split");
  teardown_libcall_runtime();
}

void test_text_split_invalid_context_root_and_return_pointer(void) {
  setup_libcall_runtime();
  ITEM_t *context_root = make_root_item("split_context");
  ASSERT_NOT_NULL(context_root);
  ITEM_t *caller = test_item_set_value(context_root, "caller",
                                       (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  RuntimeContext context = *test_ctx();
  context.itemstore = itemstore_owner(context_root);
  context.current_item = caller;
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  uint8_t *sentinel = (uint8_t *)(uintptr_t)0x1234u;
  ASSERT_TRUE(lc_text_split(&context, sentinel, context_root) == sentinel);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ITEM_t *error = find_item(context_root, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ITEM_t *message = find_item(context_root, "error.msg");
  ITEM_t *provenance = find_item(context_root, "error.item");
  ASSERT_NOT_NULL(message);
  ASSERT_NOT_NULL(provenance);
  ASSERT_TRUE(strstr(item_value(message)->s, "text.split") != NULL);
  ASSERT_EQ_INT(VALUE_str, item_value(provenance)->type);
  ASSERT_TRUE(strcmp(item_value(provenance)->s, "caller") == 0);
  destroy_item(context_root);
  teardown_libcall_runtime();
}

static void assert_split_allocation_failure(const char *text,
                                            const char *separator,
                                            long failure) {
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(separator)}});
  alloc_test_fail_after(failure);
  (void)lc_text_split(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx),
                              "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(item_value(message)->s, "prior diagnostic") != NULL);
}

void test_text_split_distinct_allocation_failures(void) {
  setup_libcall_runtime();
  assert_split_allocation_failure("", ",", 0);
  assert_split_allocation_failure("original", "", 0);
  assert_split_allocation_failure("original", "", 1);
  assert_split_allocation_failure("a,b,c", ",", 2);
  assert_split_allocation_failure("a,b,c", ",", 3);
  assert_split_allocation_failure("a,b,c", ",", 4);
  assert_split_allocation_failure("a,b,c", ",", 5);
  teardown_libcall_runtime();
}

void test_text_split_list_limit_boundaries(void) {
  setup_libcall_runtime();
  size_t max = SIN_LIST_MAX_ELEMENTS;
  size_t over_len = (max + 1u) * 2u - 1u;
  char *input = malloc(over_len + 1u);
  ASSERT_NOT_NULL(input);
  for (size_t i = 0; i < over_len; ++i) input[i] = (i % 2u) ? ',' : 'x';
  input[over_len] = '\0';
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = input}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(",")}});
  (void)lc_text_split(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                          "error.msg"))->s,
                     "prior diagnostic") != NULL);
  teardown_libcall_runtime();
}

void test_text_split_failures_preserve_diagnostic_and_cleanup(void) {
  setup_libcall_runtime();
  assert_split_allocation_failure("", ",", 0);
  assert_split_allocation_failure("original", "", 0);
  assert_split_allocation_failure("original", "", 1);
  assert_split_allocation_failure("a,b,c", ",", 2);
  assert_split_allocation_failure("a,b,c", ",", 3);
  assert_split_allocation_failure("a,b,c", ",", 4);
  assert_split_allocation_failure("a,b,c", ",", 5);

  const char *ordinary[] = {"a", "b"};
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  assert_split_strings(call_split("a,b", ","), ordinary, 2);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                          "error.msg"))->s,
                     "prior diagnostic") != NULL);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  assert_split_strings(call_split("", ","), NULL, 0);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                          "error.msg"))->s,
                     "prior diagnostic") != NULL);
  teardown_libcall_runtime();
}

void test_text_split_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.parts = text.split{\"a::b::c\", \"::\"};")}};
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);
  ITEM_t *parts = find_item(itemstore_root(config.itemstore_ctx),
                            "result.parts");
  ASSERT_NOT_NULL(parts);
  ASSERT_EQ_INT(VALUE_list, item_value(parts)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(parts)->list));
  const char *expected[] = {"a", "b", "c"};
  for (size_t i = 0; i < 3; ++i) {
    const VALUE_t *element = sin_list_get(item_value(parts)->list, i);
    ASSERT_NOT_NULL(element);
    ASSERT_EQ_INT(VALUE_str, element->type);
    ASSERT_TRUE(strcmp(element->s, expected[i]) == 0);
  }

  const char *invalid[] = {"text.split{\"a\"};",
                           "text.split{\"a\", \",\", \",\"};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}

void test_text_words_registry_contract(void) {
  uint8_t library = 0, call = 0, args = 0;
  ASSERT_TRUE(libcall_lookup_pair("text", "words", &library, &call, &args));
  ASSERT_EQ_INT(10, library);
  ASSERT_EQ_INT(2, call);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(library, call) == lc_text_words);
  ASSERT_TRUE(libcall_pair_arg_count(library, call, &args));
  ASSERT_EQ_INT(1, args);
}

void test_text_words_literal_whitespace_and_utf8(void) {
  setup_libcall_runtime();
  const char *expected[] = {"one", "two", "three", "four\\five",
                            "caf\xC3\xA9"};
  assert_words_strings(call_words(" \tone\n two\vthree\ffour\\five\r caf\xC3\xA9 "),
                       expected, 5);
  const char *punctuated[] = {"put", "brass", "key", "in", "box"};
  assert_words_strings(call_words("   put   brass key   in box  "), punctuated,
                       5);
  const char *quoted[] = {"say", "\"hello", "there\""};
  assert_words_strings(call_words("say \"hello there\""), quoted, 3);
  teardown_libcall_runtime();
}

void test_text_words_empty_single_and_ownership(void) {
  setup_libcall_runtime();
  assert_words_strings(call_words(""), NULL, 0);
  assert_words_strings(call_words(" \t\n\v\f\r"), NULL, 0);
  const char *single[] = {"single"};
  assert_words_strings(call_words("single"), single, 1);

  VALUE_t result = call_words("first second");
  ASSERT_EQ_INT(VALUE_list, result.type);
  VALUE_t *first = (VALUE_t *)sin_list_get(result.list, 0);
  ASSERT_NOT_NULL(first);
  first->s[0] = 'F';
  ASSERT_TRUE(strcmp(sin_list_get(result.list, 1)->s, "second") == 0);
  value_free(&result);
  teardown_libcall_runtime();
}

void test_text_words_invalid_types_and_stack_contract(void) {
  setup_libcall_runtime();
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 77}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  (void)lc_text_words(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(1, size_stack(config.vm->stack));
  ASSERT_EQ_INT(77, pop_stack(config.vm->stack).i);
  assert_invalid_args_detail_contains("text.words");

  VALUE_t invalid[] = {{VALUE_float, {.f = 1.0}},
                       {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}},
                       {VALUE_str, {.s = NULL}}};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    push_stack(config.vm->stack, invalid[i]);
    invalid[i] = VALUE_NIL;
    (void)lc_text_words(test_ctx(), NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("text.words");
  }
  teardown_libcall_runtime();
}

void test_text_words_invalid_context_root_and_return_pointer(void) {
  setup_libcall_runtime();
  ITEM_t *context_root = make_root_item("words_context");
  ASSERT_NOT_NULL(context_root);
  ITEM_t *caller = test_item_set_value(context_root, "caller",
                                       (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  RuntimeContext context = *test_ctx();
  context.itemstore = itemstore_owner(context_root);
  context.current_item = caller;
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  uint8_t *sentinel = (uint8_t *)(uintptr_t)0x1234u;
  ASSERT_TRUE(lc_text_words(&context, sentinel, context_root) == sentinel);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS,
                item_value(find_item(context_root, "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(context_root, "error.msg"))->s,
                     "text.words") != NULL);
  ASSERT_TRUE(strcmp(item_value(find_item(context_root, "error.item"))->s,
                     "caller") == 0);
  destroy_item(context_root);
  teardown_libcall_runtime();
}

static void assert_words_allocation_failure(const char *text, long failure) {
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(text)}});
  alloc_test_fail_after(failure);
  (void)lc_text_words(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);
}

void test_text_words_distinct_allocation_failures(void) {
  setup_libcall_runtime();
  for (long failure = 0; failure <= 5; ++failure)
    assert_words_allocation_failure("a b c", failure);
  teardown_libcall_runtime();
}

void test_text_words_string_limit_boundaries(void) {
  setup_libcall_runtime();
  char *maximum = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(maximum);
  memset(maximum, 'x', SIN_MAX_STRING_BYTES);
  maximum[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t result = call_words(maximum);
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT(1, sin_list_count(result.list));
  ASSERT_EQ_INT(SIN_MAX_STRING_BYTES,
                strlen(sin_list_get(result.list, 0)->s));
  value_free(&result);
  free(maximum);

  char *too_large = malloc(SIN_MAX_STRING_BYTES + 2u);
  ASSERT_NOT_NULL(too_large);
  memset(too_large, 'y', SIN_MAX_STRING_BYTES + 1u);
  too_large[SIN_MAX_STRING_BYTES + 1u] = '\0';
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  result = call_words(too_large);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);
  free(too_large);
  teardown_libcall_runtime();
}

void test_text_words_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.words = text.words{\"a\\tb\\n c\"};")}};
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);
  ITEM_t *words = find_item(itemstore_root(config.itemstore_ctx),
                            "result.words");
  ASSERT_NOT_NULL(words);
  const char *expected[] = {"a", "b", "c"};
  assert_words_strings((VALUE_t){VALUE_list, {.list = sin_list_retain(
      item_value(words)->list)}}, expected, 3);

  const char *invalid[] = {"text.words{\"a\", \"b\"};",
                           "text.words{\"a\", \"b\", \"c\"};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}

void test_text_lines_registry_contract(void) {
  uint8_t library = 0, call = 0, args = 0;
  ASSERT_TRUE(libcall_lookup_pair("text", "lines", &library, &call, &args));
  ASSERT_EQ_INT(10, library);
  ASSERT_EQ_INT(3, call);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(library, call) == lc_text_lines);
  ASSERT_TRUE(libcall_pair_arg_count(library, call, &args));
  ASSERT_EQ_INT(1, args);
}

void test_text_lines_literal_terminators_and_bytes(void) {
  setup_libcall_runtime();
  const char *example[] = {"this", "is", "a", "string"};
  assert_lines_strings(call_lines("this\nis\na\nstring"), example, 4);
  const char *mixed[] = {"one", "two", "three", "four"};
  assert_lines_strings(call_lines("one\ntwo\r\n\nthree\r\nfour"), mixed, 4);
  const char *literal[] = {"left\rmiddle", "caf\xC3\xA9", "quote\\value"};
  assert_lines_strings(call_lines("left\rmiddle\ncaf\xC3\xA9\r\nquote\\value"),
                       literal, 3);
  teardown_libcall_runtime();
}

void test_text_lines_empty_and_ownership(void) {
  setup_libcall_runtime();
  assert_lines_strings(call_lines(""), NULL, 0);
  assert_lines_strings(call_lines("\n\r\n\n"), NULL, 0);
  const char *single[] = {"single"};
  assert_lines_strings(call_lines("single"), single, 1);

  VALUE_t result = call_lines("first\nsecond");
  ASSERT_EQ_INT(VALUE_list, result.type);
  VALUE_t *first = (VALUE_t *)sin_list_get(result.list, 0);
  ASSERT_NOT_NULL(first);
  first->s[0] = 'F';
  ASSERT_TRUE(strcmp(sin_list_get(result.list, 1)->s, "second") == 0);
  value_free(&result);
  teardown_libcall_runtime();
}

void test_text_lines_invalid_types_and_stack_contract(void) {
  setup_libcall_runtime();
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 77}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  (void)lc_text_lines(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(1, size_stack(config.vm->stack));
  ASSERT_EQ_INT(77, pop_stack(config.vm->stack).i);
  assert_invalid_args_detail_contains("text.lines");

  VALUE_t invalid[] = {
      {VALUE_nil, {.i = 0}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}},
      {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}}};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    push_stack(config.vm->stack, invalid[i]);
    invalid[i] = VALUE_NIL;
    (void)lc_text_lines(test_ctx(), NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("text.lines");
  }

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = NULL}});
  (void)lc_text_lines(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.lines");
  teardown_libcall_runtime();
}

void test_text_lines_invalid_context_root_and_return_pointer(void) {
  setup_libcall_runtime();
  ITEM_t *context_root = make_root_item("lines_context");
  ASSERT_NOT_NULL(context_root);
  ITEM_t *caller = test_item_set_value(context_root, "caller",
                                       (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  RuntimeContext context = *test_ctx();
  context.itemstore = itemstore_owner(context_root);
  context.current_item = caller;
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  uint8_t *sentinel = (uint8_t *)(uintptr_t)0x1234u;
  ASSERT_TRUE(lc_text_lines(&context, sentinel, context_root) == sentinel);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS,
                item_value(find_item(context_root, "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(context_root, "error.msg"))->s,
                     "text.lines") != NULL);
  ASSERT_TRUE(strcmp(item_value(find_item(context_root, "error.item"))->s,
                     "caller") == 0);
  destroy_item(context_root);
  teardown_libcall_runtime();
}

static void assert_lines_allocation_failure(const char *text, long failure) {
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(text)}});
  alloc_test_fail_after(failure);
  (void)lc_text_lines(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);
}

void test_text_lines_distinct_allocation_failures(void) {
  setup_libcall_runtime();
  for (long failure = 0; failure <= 6; ++failure)
    assert_lines_allocation_failure("a\nb\nc", failure);
  teardown_libcall_runtime();
}

void test_text_lines_string_and_list_boundaries(void) {
  setup_libcall_runtime();
  char *maximum = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(maximum);
  memset(maximum, 'x', SIN_MAX_STRING_BYTES);
  maximum[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t result = call_lines(maximum);
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT(1, sin_list_count(result.list));
  ASSERT_EQ_INT(SIN_MAX_STRING_BYTES,
                strlen(sin_list_get(result.list, 0)->s));
  value_free(&result);
  free(maximum);

  size_t over_len = (SIN_LIST_MAX_ELEMENTS + 1u) * 2u - 1u;
  char *too_many = malloc(over_len + 1u);
  ASSERT_NOT_NULL(too_many);
  for (size_t i = 0; i < over_len; ++i)
    too_many[i] = (i % 2u) == 0 ? 'x' : '\n';
  too_many[over_len] = '\0';
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  result = call_lines(too_many);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  free(too_many);
  teardown_libcall_runtime();
}

void test_text_lines_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.lines = text.lines{\"a\\nb\\r\\nc\"};")}};
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);
  ITEM_t *lines = find_item(itemstore_root(config.itemstore_ctx),
                            "result.lines");
  ASSERT_NOT_NULL(lines);
  const char *expected[] = {"a", "b", "c"};
  assert_lines_strings((VALUE_t){VALUE_list, {.list = sin_list_retain(
      item_value(lines)->list)}}, expected, 3);

  const char *invalid[] = {"text.lines{\"a\", \"b\"};",
                           "text.lines{\"a\", \"b\", \"c\"};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}

static SIN_LIST_t *make_join_string_list(const char *const *strings,
                                         size_t count) {
  if (count == 0) return sin_list_build_owned(NULL, 0);
  VALUE_t *values = calloc(count, sizeof(*values));
  ASSERT_NOT_NULL(values);
  for (size_t i = 0; i < count; ++i) {
    values[i] = (VALUE_t){VALUE_str, {.s = strdup(strings[i])}};
    ASSERT_NOT_NULL(values[i].s);
  }
  SIN_LIST_t *list = sin_list_build_owned(values, count);
  ASSERT_NOT_NULL(list);
  free(values);
  return list;
}

static VALUE_t call_join(SIN_LIST_t *list, const char *separator) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_list, {.list = list}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(separator)}});
  (void)lc_text_join(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

void test_text_join_registry_contract(void) {
  uint8_t library = 0, call = 0, args = 0;
  ASSERT_TRUE(libcall_lookup_pair("text", "join", &library, &call, &args));
  ASSERT_EQ_INT(10, library);
  ASSERT_EQ_INT(1, call);
  ASSERT_EQ_INT(2, args);
  ASSERT_TRUE(libcall_func_pair(library, call) == lc_text_join);
  ASSERT_TRUE(libcall_pair_arg_count(library, call, &args));
  ASSERT_EQ_INT(2, args);
  ASSERT_TRUE(libcall_lookup_pair("text", "split", &library, &call, &args));
  ASSERT_EQ_INT(0, call);
  ASSERT_EQ_INT(2, args);
}

void test_text_join_literal_fields_and_utf8(void) {
  setup_libcall_runtime();
  const char *fields[] = {"one", "", "three"};
  VALUE_t result = call_join(make_join_string_list(fields, 3), "::");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "one::::three") == 0);
  value_free(&result);

  const char *unicode[] = {"caf\xC3\xA9", "\xE6\x9D\xB1"};
  result = call_join(make_join_string_list(unicode, 2), "<->");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "caf\xC3\xA9<->\xE6\x9D\xB1") == 0);
  value_free(&result);

  const char *source_fields[] = {"first", "second"};
  SIN_LIST_t *source = make_join_string_list(source_fields, 2);
  SIN_LIST_t *surviving = sin_list_retain(source);
  result = call_join(source, "|");
  ASSERT_EQ_INT(VALUE_str, result.type);
  sin_list_get(surviving, 0)->s[0] = 'M';
  ASSERT_TRUE(strcmp(result.s, "first|second") == 0);
  value_free(&result);
  sin_list_release(surviving);
  teardown_libcall_runtime();
}

void test_text_join_empty_and_separator_behavior(void) {
  setup_libcall_runtime();
  VALUE_t result = call_join(make_join_string_list(NULL, 0), ",");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "") == 0);
  value_free(&result);
  const char *fields[] = {"a", "", "b"};
  result = call_join(make_join_string_list(fields, 3), "");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "ab") == 0);
  value_free(&result);
  const char *single[] = {"single"};
  result = call_join(make_join_string_list(single, 1), "separator");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "single") == 0);
  value_free(&result);
  teardown_libcall_runtime();
}

void test_text_join_invalid_types_and_stack_contract(void) {
  setup_libcall_runtime();
  const char *fields[] = {"valid"};
  VALUE_t invalid_lists[] = {
      {VALUE_nil, {.i = 0}},
      {VALUE_int, {.i = 7}},
      {VALUE_list, {.list = NULL}}};
  for (size_t i = 0; i < sizeof(invalid_lists) / sizeof(invalid_lists[0]); ++i) {
    push_stack(config.vm->stack, invalid_lists[i]);
    invalid_lists[i] = VALUE_NIL;
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup(",")}});
    (void)lc_text_join(test_ctx(), NULL, NULL);
    VALUE_t result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains("text.join");
  }
  SIN_LIST_t *heterogeneous = make_join_string_list(fields, 1);
  VALUE_t extra = (VALUE_t){VALUE_int, {.i = 42}};
  SIN_LIST_t *invalid = sin_list_append(heterogeneous, &extra);
  ASSERT_NOT_NULL(invalid);
  sin_list_release(heterogeneous);
  push_stack(config.vm->stack, (VALUE_t){VALUE_list, {.list = invalid}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(",")}});
  (void)lc_text_join(test_ctx(), NULL, NULL);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.join");

  VALUE_t malformed = {VALUE_str, {.s = NULL}};
  SIN_LIST_t *with_null = sin_list_build_owned(&malformed, 1);
  ASSERT_NOT_NULL(with_null);
  push_stack(config.vm->stack, (VALUE_t){VALUE_list, {.list = with_null}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(",")}});
  (void)lc_text_join(test_ctx(), NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.join");
  teardown_libcall_runtime();
}

void test_text_join_invalid_context_root_and_return_pointer(void) {
  setup_libcall_runtime();
  ITEM_t *context_root = make_root_item("join_context");
  ASSERT_NOT_NULL(context_root);
  ITEM_t *caller = test_item_set_value(context_root, "caller",
                                       (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  RuntimeContext context = *test_ctx();
  context.itemstore = itemstore_owner(context_root);
  context.current_item = caller;
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = make_join_string_list(NULL, 0)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_nil, {.i = 0}});
  uint8_t *sentinel = (uint8_t *)(uintptr_t)0x1234u;
  ASSERT_TRUE(lc_text_join(&context, sentinel, context_root) == sentinel);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ITEM_t *error = find_item(context_root, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ITEM_t *message = find_item(context_root, "error.msg");
  ITEM_t *provenance = find_item(context_root, "error.item");
  ASSERT_NOT_NULL(message);
  ASSERT_NOT_NULL(provenance);
  ASSERT_TRUE(strstr(item_value(message)->s, "text.join") != NULL);
  ASSERT_EQ_INT(VALUE_str, item_value(provenance)->type);
  ASSERT_TRUE(strcmp(item_value(provenance)->s, "caller") == 0);
  destroy_item(context_root);
  teardown_libcall_runtime();
}

void test_text_join_string_limit_boundaries(void) {
  setup_libcall_runtime();
  char *maximum = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(maximum);
  memset(maximum, 'x', SIN_MAX_STRING_BYTES);
  maximum[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t element = {VALUE_str, {.s = maximum}};
  SIN_LIST_t *list = sin_list_build_owned(&element, 1);
  ASSERT_NOT_NULL(list);
  VALUE_t result = call_join(list, ",");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_EQ_INT(SIN_MAX_STRING_BYTES, strlen(result.s));
  value_free(&result);

  char *too_large = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(too_large);
  memset(too_large, 'y', SIN_MAX_STRING_BYTES);
  too_large[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t values[] = {{VALUE_str, {.s = strdup("z")}},
                      {VALUE_str, {.s = too_large}}};
  list = sin_list_build_owned(values, 2);
  ASSERT_NOT_NULL(list);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  result = call_join(list, ",");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);

  char *valid_limit = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(valid_limit);
  memset(valid_limit, 'v', SIN_MAX_STRING_BYTES);
  valid_limit[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t invalid_values[] = {{VALUE_str, {.s = valid_limit}},
                              {VALUE_int, {.i = 7}}};
  list = sin_list_build_owned(invalid_values, 2);
  ASSERT_NOT_NULL(list);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  result = call_join(list, ",");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("text.join");
  teardown_libcall_runtime();
}

static void assert_join_allocation_failure(const char *separator,
                                           long failure) {
  const char *fields[] = {"a", "b", "c"};
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = make_join_string_list(fields, 3)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(separator)}});
  alloc_test_fail_after(failure);
  (void)lc_text_join(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);
}

void test_text_join_distinct_allocation_failures(void) {
  setup_libcall_runtime();
  assert_join_allocation_failure(",", 0);
  teardown_libcall_runtime();
}

void test_text_join_failures_preserve_diagnostic_and_cleanup(void) {
  setup_libcall_runtime();
  assert_join_allocation_failure(",", 0);
  const char *fields[] = {"a", "b"};
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  VALUE_t result = call_join(make_join_string_list(fields, 2), ",");
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "a,b") == 0);
  value_free(&result);
  ASSERT_TRUE(strstr(item_value(find_item(itemstore_root(config.itemstore_ctx),
                                         "error.msg"))->s,
                     "prior diagnostic") != NULL);
  teardown_libcall_runtime();
}

void test_text_join_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.joined = text.join{#[\"a\", \"b\", \"c\"], \"::\"};")}};
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);
  ITEM_t *joined = find_item(itemstore_root(config.itemstore_ctx),
                             "result.joined");
  ASSERT_NOT_NULL(joined);
  ASSERT_EQ_INT(VALUE_str, item_value(joined)->type);
  ASSERT_TRUE(strcmp(item_value(joined)->s, "a::b::c") == 0);

  const char *invalid[] = {"text.join{#[\"a\"]};",
                           "text.join{#[\"a\"], \"::\", \"extra\"};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                                &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}
