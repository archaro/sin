#include "item.h"
#include <errno.h>
#include "test_helpers.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "error.h"
#include "item.h"
#include "interpret.h"
#include "libcall.h"
#include "memory.h"
#include "test_assert.h"
#include "value.h"
#include "list.h"
#include "itemref.h"
#include "version.h"
#include "vm.h"

CONFIG_t config;
static RuntimeContext test_runtime_ctx;
static RuntimeContext *test_ctx(void) {
  runtime_context_init(&test_runtime_ctx, config.vm);
  test_runtime_ctx.itemstore = config.itemstore_ctx;
  test_runtime_ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  test_runtime_ctx.srcroot = config.srcroot;
  return &test_runtime_ctx;
}
uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

static VALUE_t vstr(const char *s) {
  VALUE_t v = {VALUE_str, {.s = strdup(s)}};
  return v;
}

static void setup_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
}

static void assert_bool(VALUE_t v, int expected) {
  ASSERT_EQ_INT(VALUE_bool, v.type);
  ASSERT_EQ_INT(expected, v.i);
}

static ITEM_t *assert_string_item(const char *name, const char *contains) {
  ITEM_t *item = find_item(itemstore_root(config.itemstore_ctx), name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_str, item_value(item)->type);
  ASSERT_NOT_NULL(item_value(item)->s);
  if (contains) {
    ASSERT_TRUE(strstr(item_value(item)->s, contains) != NULL);
  }
  return item;
}

static ITEM_t *assert_int_item(const char *name, int64_t expected) {
  ITEM_t *item = find_item(itemstore_root(config.itemstore_ctx), name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_int, item_value(item)->type);
  ASSERT_EQ_INT(expected, item_value(item)->i);
  return item;
}

static ITEM_t *assert_nil_item(const char *name) {
  ITEM_t *item = find_item(itemstore_root(config.itemstore_ctx), name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_value, item_kind(item));
  ASSERT_EQ_INT(VALUE_nil, item_value(item)->type);
  return item;
}

static void assert_compile_success_bool(const char *source) {
  push_stack(config.vm->stack, vstr(source));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 1);
}

static void assert_error_fields_nil(void) {
  static const char *const names[] = {
    "error", "error.msg", "error.item", "error.code", "error.stage",
    "error.file", "error.line", "error.column", "error.excerpt"
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    ITEM_t *field = find_item(itemstore_root(config.itemstore_ctx), names[i]);
    ASSERT_NOT_NULL(field);
    ASSERT_EQ_INT(VALUE_nil, item_value(field)->type);
  }
}

static void assert_only_temp_item_named(const char *expected_name) {
  const char *prefix = "__sys_compile_tmp__";
  size_t prefix_len = strlen(prefix);
  size_t found = 0;
  ITEM_t *root = itemstore_root(config.itemstore_ctx);
  for (size_t i = 0; i < item_child_count(root); i++) {
    ITEM_t *child = item_child_at(root, i);
    const char *name = item_layer_name(child);
    if (name != NULL && strncmp(name, prefix, prefix_len) == 0) {
      ASSERT_NOT_NULL(expected_name);
      ASSERT_TRUE(strcmp(name, expected_name) == 0);
      found++;
    }
  }
  ASSERT_EQ_INT(expected_name ? 1 : 0, found);
}

void test_sys_itemref_dynamic_calls(void) {
  setup_runtime();
  assert_compile_success_bool(
      "zero = code ( return 7; );"
      "one = code {@x} ( return @x; );"
      "ordered = code {@a, @b, @c} ( return #[@a, @b, @c]; );"
      "nested = code ( @r = sys.itemref{\"ordered\"}; return sys.call{@r, #[9, 8, 7]}; );"
      "producer_one = code ( results.order = results.order * 10 + 1; return 11; );"
      "producer_two = code ( results.order = results.order * 10 + 2; return 22; );"
      "run = code ("
      "  @z = sys.itemref{\"zero\"};"
      "  @o = sys.itemref{\"one\"};"
      "  @ordered = sys.itemref{\"ordered\"};"
      "  results.exact = sys.call{@ordered, #[1, 2, 3]};"
      "  results.missing = sys.call{@ordered, #[1]};"
      "  results.excess = sys.call{@ordered, #[1, 2, 3, 4]};"
      "  @nested = sys.itemref{\"nested\"}; results.nested = sys.fetch{@nested};"
      "  results.order = 0;"
      "  @producer = sys.itemref{\"producer\"};"
      "  results.produced = sys.call{@ordered, #[producer_one, producer_two]};"
      "  @aggref = sys.itemref{\"zero\"}; source.value = #[\"text\", #[1, 2], @aggref];"
      "  results.missing_target = sys.fetch{sys.itemref{\"missing\"}};"
      "  results.noncode_target = sys.call{sys.itemref{\"source.value\"}, #[]};"
      "  @weak = sys.itemref{\"weak.target\"};"
      "  results.weak_before = sys.fetch{@weak};"
      "  weak.target = 11; results.weak_created = sys.fetch{@weak};"
      "  sys.delete{@weak}; results.weak_deleted = sys.fetch{@weak};"
      "  weak.target = 22; results.weak_recreated = sys.fetch{@weak};"
      "  @aggregate = sys.itemref{\"source.value\"};"
      "  results.aggregate = sys.fetch{@aggregate};"
      "  sys.delete{@aggregate};"
      "  results.zero = sys.fetch{@z};"
      "  results.param_nil = sys.fetch{@o};"
      "  MiXeD.Target = code (return 19);"
      "  results.mixed_call = mIxEd.tArGeT{};"
      "  @mixed_name = \"MiXeD.TaRgEt\";"
      "  results.mixed_dynamic = [@mixed_name];"
      "  mixed_dynamic.[\"ChIlD\"] = 23;"
      "  results.mixed_child = MIXED_DYNAMIC.child;"
      "  results.ordinary_string = \"MiXeD.Value\";"
      ");");
  ITEM_t *run = find_item(itemstore_root(config.itemstore_ctx), "run");
  ASSERT_NOT_NULL(run);
  RuntimeContext *strict_ctx = test_ctx();
  strict_ctx->strict_runtime_contracts = true;
  VALUE_t result = interpret(strict_ctx, run);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ITEM_t *strict_error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ITEM_t *strict_message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ITEM_t *strict_origin = find_item(itemstore_root(config.itemstore_ctx), "error.item");
  ASSERT_NOT_NULL(strict_error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(strict_error)->i);
  ASSERT_NOT_NULL(strict_message);
  ASSERT_TRUE(strstr(item_value(strict_message)->s, "sys.call discarded extra argument") != NULL);
  ASSERT_NOT_NULL(strict_origin);
  ASSERT_TRUE(strcmp(item_value(strict_origin)->s, "run") == 0);

  ITEM_t *exact = assert_int_item("results.order", 12);
  (void)exact;
  ITEM_t *stored = find_item(itemstore_root(config.itemstore_ctx), "results.exact");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(stored)->list));
  ASSERT_EQ_INT(1, sin_list_get(item_value(stored)->list, 0)->i);
  ASSERT_EQ_INT(2, sin_list_get(item_value(stored)->list, 1)->i);
  ASSERT_EQ_INT(3, sin_list_get(item_value(stored)->list, 2)->i);
  stored = find_item(itemstore_root(config.itemstore_ctx), "results.missing");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(stored)->list));
  ASSERT_EQ_INT(1, sin_list_get(item_value(stored)->list, 0)->i);
  ASSERT_EQ_INT(VALUE_nil, sin_list_get(item_value(stored)->list, 1)->type);
  ASSERT_EQ_INT(VALUE_nil, sin_list_get(item_value(stored)->list, 2)->type);
  stored = find_item(itemstore_root(config.itemstore_ctx), "results.excess");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(stored)->list));
  ASSERT_EQ_INT(3, sin_list_get(item_value(stored)->list, 2)->i);
  stored = find_item(itemstore_root(config.itemstore_ctx), "results.nested");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(9, sin_list_get(item_value(stored)->list, 0)->i);
  ASSERT_EQ_INT(8, sin_list_get(item_value(stored)->list, 1)->i);
  ASSERT_EQ_INT(7, sin_list_get(item_value(stored)->list, 2)->i);
  stored = find_item(itemstore_root(config.itemstore_ctx), "results.produced");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(11, sin_list_get(item_value(stored)->list, 0)->i);
  ASSERT_EQ_INT(22, sin_list_get(item_value(stored)->list, 1)->i);
  assert_nil_item("results.missing_target");
  assert_nil_item("results.noncode_target");
  assert_nil_item("results.weak_before");
  assert_int_item("results.weak_created", 11);
  assert_nil_item("results.weak_deleted");
  assert_int_item("results.weak_recreated", 22);
  stored = find_item(itemstore_root(config.itemstore_ctx), "results.aggregate");
  ASSERT_NOT_NULL(stored);
  ASSERT_EQ_INT(VALUE_list, item_value(stored)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(stored)->list));
  ASSERT_EQ_INT(VALUE_str, sin_list_get(item_value(stored)->list, 0)->type);
  ASSERT_TRUE(strcmp(sin_list_get(item_value(stored)->list, 0)->s, "text") == 0);
  ASSERT_EQ_INT(VALUE_list, sin_list_get(item_value(stored)->list, 1)->type);
  ASSERT_EQ_INT(2, sin_list_count(sin_list_get(item_value(stored)->list, 1)->list));
  ASSERT_EQ_INT(1, sin_list_get(sin_list_get(item_value(stored)->list, 1)->list, 0)->i);
  ASSERT_EQ_INT(VALUE_itemref, sin_list_get(item_value(stored)->list, 2)->type);
  ASSERT_TRUE(strcmp(sin_itemref_path(sin_list_get(item_value(stored)->list, 2)->itemref), "zero") == 0);
  assert_int_item("results.zero", 7);
  assert_nil_item("results.param_nil");
  assert_int_item("results.mixed_call", 19);
  assert_int_item("results.mixed_dynamic", 19);
  assert_int_item("results.mixed_child", 23);
  ITEM_t *ordinary_string = assert_string_item("results.ordinary_string",
                                               "MiXeD.Value");
  ASSERT_TRUE(strcmp(item_value(ordinary_string)->s, "MiXeD.Value") == 0);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "source.value") == NULL);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  teardown_runtime();
}

static uint64_t parse_temp_counter(const char *name) {
  const char *prefix = "__sys_compile_tmp__";
  size_t prefix_len = strlen(prefix);
  ASSERT_NOT_NULL(name);
  ASSERT_TRUE(strncmp(name, prefix, prefix_len) == 0);

  errno = 0;
  char *end = NULL;
  unsigned long long parsed = strtoull(name + prefix_len, &end, 10);
  ASSERT_EQ_INT(0, errno);
  ASSERT_TRUE(end != name + prefix_len && *end == '\0');
  return (uint64_t)parsed;
}

static void assert_compile_stdout(const char *source, const char *expected) {
  fflush(stdout);
  FILE *capture = tmpfile();
  ASSERT_NOT_NULL(capture);
  int saved_stdout = dup(STDOUT_FILENO);
  ASSERT_TRUE(saved_stdout >= 0);
  ASSERT_TRUE(dup2(fileno(capture), STDOUT_FILENO) >= 0);

  push_stack(config.vm->stack, vstr(source));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t result = pop_stack(config.vm->stack);
  fflush(stdout);

  ASSERT_TRUE(dup2(saved_stdout, STDOUT_FILENO) >= 0);
  close(saved_stdout);
  rewind(capture);
  char output[128] = {0};
  size_t used = fread(output, 1, sizeof(output) - 1u, capture);
  output[used] = '\0';
  fclose(capture);

  assert_bool(result, 1);
  ASSERT_TRUE(strcmp(output, expected) == 0);
}

static uint8_t *inject_interrupt_after_push_bool(RuntimeContext *ctx,
                                                 uint8_t *nextop,
                                                 ITEM_t *item) {
  (void)item;
  ASSERT_NOT_NULL(ctx->interrupt_pending);
  push_stack(ctx->vm->stack,
             *nextop ? (VALUE_t){VALUE_bool, {.i = 1}} : VALUE_FALSE);
  *ctx->interrupt_pending = 1;
  return nextop + 1;
}

void test_sys_compile_libcall_runtime(void) {
  setup_runtime();

  assert_compile_success_bool("foo = 42;");
  ITEM_t *foo = assert_int_item("foo", 42);
  ASSERT_EQ_INT(ITEM_value, item_kind(foo));
  assert_compile_success_bool("syscompile.observed = foo;");
  assert_int_item("syscompile.observed", 42);

  assert_compile_success_bool("foo = code {@in} ( return @in+10; );");
  foo = find_item(itemstore_root(config.itemstore_ctx), "foo");
  ASSERT_NOT_NULL(foo);
  ASSERT_EQ_INT(ITEM_code, item_kind(foo));
  ASSERT_NOT_NULL(item_bytecode(foo));
  ASSERT_TRUE(item_bytecode_length(foo) >= 8u);
  ASSERT_EQ_INT(1, item_bytecode(foo)[7]);
  assert_compile_success_bool("syscompile.observed = foo{10};");
  assert_int_item("syscompile.observed", 20);

  assert_compile_stdout("sys.log{\"Hello\\n\"};", "Hello\n");
  assert_only_temp_item_named(NULL);

  /* Escaped \n remains valid source inside code(...). A literal LF inside a
   * quoted code-body string must fail during parsing, not validation. */
  assert_compile_success_bool(
      "newline_source.ok = code ( sys.log{\"Hello\\n\"}; );");
  assert_compile_stdout("newline_source.ok;", "Hello\n");

  push_stack(config.vm->stack,
             vstr("newline_source.bad = code ( sys.log{\"Hello\nworld\"}; );"));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  assert_string_item("error.stage", "PARSE");
  assert_string_item("error.msg", "Newline in string.");
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx),
                        "newline_source.bad") == NULL);

  /* Raw strings are passed byte-for-byte to sys.compile, so the \n below is
   * interpreted only by the compiler which consumes raw_source. */
  assert_compile_success_bool(
      "raw_source = \"\"\"sys.log{\"Hello\\n\"};\"\"\";");
  ITEM_t *raw_source = assert_string_item("raw_source", NULL);
  ASSERT_TRUE(strcmp(item_value(raw_source)->s,
                     "sys.log{\"Hello\\n\"};") == 0);
  assert_compile_stdout("sys.compile{raw_source};", "Hello\n");

  /* A raw string inside code(...) must also protect parentheses and preserve
   * literal newlines while the outer lexer captures the code body. */
  assert_compile_success_bool(
      "raw_code = code ( raw.result = \"\"\"left )\nright\\n\"\"\"; );");
  assert_compile_success_bool("raw_code;");
  ITEM_t *raw_result = assert_string_item("raw.result", NULL);
  ASSERT_TRUE(strcmp(item_value(raw_result)->s,
                     "left )\nright\\n") == 0);

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS, "stale error",
                 NULL);
  assert_compile_success_bool("prior_error_cleared = true;");
  assert_error_fields_nil();

  assert_compile_success_bool("@l = true;");
  assert_compile_success_bool("false;");
  assert_compile_success_bool("foo.bar = false;");
  assert_compile_success_bool("@l = false; @l == false;");
  assert_compile_success_bool("@l = false; if @l == false then sys.log{\"False\"}; endif;");
  assert_compile_success_bool("if 1 then sys.log{\"int truthy\"}; endif;");
  assert_compile_success_bool("if \"\" then sys.log{\"empty string truthy\"}; endif;");
  assert_compile_success_bool("sys.exists{\"foo\"}; sys.delete{\"foo\"}; sys.nthname{\"foo\", 0}; sys.rootname{0};");

  assert_compile_success_bool(
      "introspection.callee = code ("
      " introspection.results.callee_this = sys.thisitem;"
      " introspection.results.callee_parent = sys.parentitem;"
      ");"
      "introspection.caller = code ("
      " introspection.results.caller_before = sys.thisitem;"
      " introspection.callee;"
      " introspection.results.caller_after = sys.thisitem;"
      " introspection.results.caller_parent = sys.parentitem;"
      ");"
      "introspection_top = code ("
      " introspection.results.top_this = sys.thisitem;"
      " introspection.results.top_parent = sys.parentitem;"
      ");"
      "introspection.caller;"
      "introspection_top;"
      "introspection.results.item_type = sys.itemtype{\"introspection.caller\"};"
      "introspection.results.child_count = sys.childcount{\"introspection\"};"
      "introspection.results.root_count = sys.rootcount;"
      "introspection.results.version = sys.version;"
      "introspection.results.now = sys.now;"
      "introspection.results.monotime = sys.monotime;"
  );
  ITEM_t *introspection_value = assert_string_item(
      "introspection.results.callee_this", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection.callee") == 0);
  introspection_value = assert_string_item(
      "introspection.results.callee_parent", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_before", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection.caller") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_after", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection.caller") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_parent", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection") == 0);
  introspection_value = assert_string_item(
      "introspection.results.top_this", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "introspection_top") == 0);
  assert_nil_item("introspection.results.top_parent");
  introspection_value = assert_string_item(
      "introspection.results.item_type", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "code") == 0);
  ITEM_t *introspection_count = find_item(
      itemstore_root(config.itemstore_ctx), "introspection.results.child_count");
  ASSERT_NOT_NULL(introspection_count);
  ASSERT_EQ_INT(VALUE_int, item_value(introspection_count)->type);
  ASSERT_TRUE(item_value(introspection_count)->i >= 3);
  introspection_count = find_item(itemstore_root(config.itemstore_ctx),
                                  "introspection.results.root_count");
  ASSERT_NOT_NULL(introspection_count);
  ASSERT_EQ_INT(VALUE_int, item_value(introspection_count)->type);
  ASSERT_TRUE(item_value(introspection_count)->i > 0);
  introspection_value = assert_string_item(
      "introspection.results.version", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, SINVERSION) == 0);
  ITEM_t *introspection_time = find_item(itemstore_root(config.itemstore_ctx),
                                         "introspection.results.now");
  ASSERT_NOT_NULL(introspection_time);
  ASSERT_EQ_INT(VALUE_int, item_value(introspection_time)->type);
  ASSERT_TRUE(item_value(introspection_time)->i > 0);
  introspection_time = find_item(itemstore_root(config.itemstore_ctx),
                                 "introspection.results.monotime");
  ASSERT_NOT_NULL(introspection_time);
  ASSERT_EQ_INT(VALUE_int, item_value(introspection_time)->type);
  ASSERT_TRUE(item_value(introspection_time)->i >= 0);

  assert_compile_success_bool(
      "caller.results.direct = sys.calleritem;"
      "caller.zero = code ( nil; );"
      "caller.multiple = code {@a, @b, @c} ( nil; );"
      "caller.c = code ("
      " caller.results.c = sys.calleritem;"
      ");"
      "caller.b = code ("
      " caller.results.b_before = sys.calleritem;"
      " caller.c;"
      " caller.results.b_after = sys.calleritem;"
      ");"
      "caller.a = code ("
      " caller.results.a_before = sys.calleritem;"
      " caller.b;"
      " caller.results.a_after = sys.calleritem;"
      ");"
      "caller.compile_target = code ("
      " caller.results.compile_target = sys.calleritem;"
      ");"
      "caller.compile_host = code ("
      " @sentinel = \"caller local\";"
      " @numbers = #[4, 5, 6];"
      " caller.results.compile_host_before = sys.calleritem;"
      " sys.compile{\"@nested = \\\"nested local\\\";"
      " caller.results.compile_nested_local = @nested;"
      " caller.results.compile_temp = sys.calleritem;"
      " caller.compile_target;\"};"
      " caller.results.compile_host_after = sys.calleritem;"
      " caller.results.compile_sentinel = @sentinel;"
      " caller.results.compile_numbers = @numbers;"
      " @compile_failure = sys.compile{\"sys.exists{42};\"};"
      " caller.results.compile_failure = @compile_failure;"
      " caller.results.compile_sentinel_after_failure = @sentinel;"
      " caller.results.compile_numbers_after_failure = @numbers;"
      ");"
      "caller.compile_outer = code ("
      " caller.compile_host;"
      " caller.results.compile_outer_after = sys.calleritem;"
      ");"
      "caller.results.zero_params = sys.paramcount{\"caller.zero\"};"
      "caller.results.multiple_params = sys.paramcount{\"caller.multiple\"};"
      "caller.a;"
  );
  assert_nil_item("caller.results.direct");
  introspection_value = assert_string_item("caller.results.b_before", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.a") == 0);
  introspection_value = assert_string_item("caller.results.c", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.b") == 0);
  introspection_value = assert_string_item("caller.results.b_after", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.a") == 0);
  introspection_value = assert_string_item("caller.results.a_before",
                                            "__sys_compile_tmp__");
  char first_temp_caller[MAX_ITEM_NAME];
  int caller_written = snprintf(first_temp_caller, sizeof(first_temp_caller),
                                "%s", item_value(introspection_value)->s);
  ASSERT_TRUE(caller_written > 0 &&
              (size_t)caller_written < sizeof(first_temp_caller));
  introspection_value = assert_string_item("caller.results.a_after", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, first_temp_caller) == 0);
  assert_int_item("caller.results.zero_params", 0);
  assert_int_item("caller.results.multiple_params", 3);

  ITEM_t *compile_outer = find_item(itemstore_root(config.itemstore_ctx), "caller.compile_outer");
  ASSERT_NOT_NULL(compile_outer);
  RuntimeContext *caller_ctx = test_ctx();
  caller_ctx->invocation_callstack_floor = 29;
  caller_ctx->invocation_caller_item = foo;
  VALUE_t caller_result = interpret(caller_ctx, compile_outer);
  ASSERT_EQ_INT(VALUE_nil, caller_result.type);
  value_free(&caller_result);
  ASSERT_EQ_INT(29, caller_ctx->invocation_callstack_floor);
  ASSERT_TRUE(caller_ctx->invocation_caller_item == foo);
  introspection_value = assert_string_item("caller.results.compile_host_before",
                                            NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.compile_outer") ==
              0);
  introspection_value = assert_string_item("caller.results.compile_temp",
                                            NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.compile_host") ==
              0);
  introspection_value = assert_string_item("caller.results.compile_nested_local",
                                            NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "nested local") == 0);
  introspection_value = assert_string_item("caller.results.compile_target",
                                            "__sys_compile_tmp__");
  introspection_value = assert_string_item("caller.results.compile_host_after",
                                            NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller.compile_outer") ==
              0);
  introspection_value = assert_string_item("caller.results.compile_sentinel",
                                            NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller local") == 0);
  ITEM_t *compile_numbers = find_item(itemstore_root(config.itemstore_ctx),
                                      "caller.results.compile_numbers");
  ASSERT_NOT_NULL(compile_numbers);
  ASSERT_EQ_INT(VALUE_list, item_value(compile_numbers)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(compile_numbers)->list));
  ASSERT_EQ_INT(4, sin_list_get(item_value(compile_numbers)->list, 0)->i);
  ASSERT_EQ_INT(5, sin_list_get(item_value(compile_numbers)->list, 1)->i);
  ASSERT_EQ_INT(6, sin_list_get(item_value(compile_numbers)->list, 2)->i);
  ITEM_t *compile_failure = find_item(itemstore_root(config.itemstore_ctx),
                                      "caller.results.compile_failure");
  ASSERT_NOT_NULL(compile_failure);
  ASSERT_EQ_INT(VALUE_bool, item_value(compile_failure)->type);
  ASSERT_EQ_INT(0, item_value(compile_failure)->i);
  introspection_value = assert_string_item(
      "caller.results.compile_sentinel_after_failure", NULL);
  ASSERT_TRUE(strcmp(item_value(introspection_value)->s, "caller local") == 0);
  compile_numbers = find_item(itemstore_root(config.itemstore_ctx),
      "caller.results.compile_numbers_after_failure");
  ASSERT_NOT_NULL(compile_numbers);
  ASSERT_EQ_INT(VALUE_list, item_value(compile_numbers)->type);
  ASSERT_EQ_INT(3, sin_list_count(item_value(compile_numbers)->list));
  ASSERT_EQ_INT(4, sin_list_get(item_value(compile_numbers)->list, 0)->i);
  ASSERT_EQ_INT(5, sin_list_get(item_value(compile_numbers)->list, 1)->i);
  ASSERT_EQ_INT(6, sin_list_get(item_value(compile_numbers)->list, 2)->i);
  assert_nil_item("caller.results.compile_outer_after");
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  assert_compile_success_bool(
      "frame_init.skipped = code ("
      " if false then @never = 99; endif;"
      " frame_init.results.skipped = @never;"
      ");"
      "frame_init.direct_param = code {@p} ("
      " frame_init.results.direct_param = @p;"
      ");");
  ITEM_t *skipped = find_item(itemstore_root(config.itemstore_ctx),
                              "frame_init.skipped");
  ASSERT_NOT_NULL(skipped);
  for (int iteration = 0; iteration < 3; iteration++) {
    ASSERT_EQ_INT(-1, config.vm->stack->current);
    config.vm->stack->stack[0] =
        (VALUE_t){VALUE_int, {.i = (int64_t)(1000 + iteration)}};
    VALUE_t frame_result = interpret(test_ctx(), skipped);
    ASSERT_EQ_INT(VALUE_nil, frame_result.type);
    value_free(&frame_result);
    assert_nil_item("frame_init.results.skipped");
    ASSERT_EQ_INT(-1, config.vm->stack->current);
    ASSERT_EQ_INT(-1, config.vm->callstack->current);
  }

  ITEM_t *direct_param = find_item(itemstore_root(config.itemstore_ctx),
                                   "frame_init.direct_param");
  ASSERT_NOT_NULL(direct_param);
  config.vm->stack->stack[0] = (VALUE_t){VALUE_int, {.i = 4242}};
  VALUE_t direct_result = interpret(test_ctx(), direct_param);
  ASSERT_EQ_INT(VALUE_nil, direct_result.type);
  value_free(&direct_result);
  assert_nil_item("frame_init.results.direct_param");
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  char source_srcroot[4096];

  ASSERT_EQ_INT(0, test_temp_template(source_srcroot, sizeof source_srcroot, "sin-sys-source-compile"));
  ASSERT_NOT_NULL(mkdtemp(source_srcroot));
  config.srcroot = source_srcroot;
  assert_compile_success_bool("source_runtime.target = code ( 7; );");
  ITEM_t *source_target = find_item(itemstore_root(config.itemstore_ctx), "source_runtime.target");
  ASSERT_NOT_NULL(source_target);
  char compiled_source[] = "source_runtime.target = code ( 7; );\n";
  ASSERT_TRUE(save_itemsource_in_srcroot(source_target, compiled_source,
                                         source_srcroot));
  assert_compile_success_bool(
      "source_runtime.result = sys.source{\"source_runtime.target\"};");
  ITEM_t *source_result = assert_string_item("source_runtime.result", NULL);
  ASSERT_TRUE(strcmp(item_value(source_result)->s, compiled_source) == 0);
  char *compiled_filename = get_itemfilename_in_srcroot(source_target,
                                                        source_srcroot);
  ASSERT_NOT_NULL(compiled_filename);
  ASSERT_EQ_INT(0, unlink(compiled_filename));
  free(compiled_filename);
  char source_cleanup[512];
  int source_written = snprintf(source_cleanup, sizeof(source_cleanup),
      "%s/source_runtime/target", source_srcroot);
  ASSERT_TRUE(source_written > 0 &&
              (size_t)source_written < sizeof(source_cleanup));
  ASSERT_EQ_INT(0, rmdir(source_cleanup));
  source_written = snprintf(source_cleanup, sizeof(source_cleanup),
                            "%s/source_runtime", source_srcroot);
  ASSERT_TRUE(source_written > 0 &&
              (size_t)source_written < sizeof(source_cleanup));
  ASSERT_EQ_INT(0, rmdir(source_cleanup));
  ASSERT_EQ_INT(0, rmdir(source_srcroot));
  config.srcroot = NULL;

  push_stack(config.vm->stack, vstr("sys.log{;"));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  ITEM_t *err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ITEM_t *msg_item = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(err_item);
  ASSERT_NOT_NULL(msg_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_TRUE(item_value(err_item)->i != ERR_NOERROR);
  ASSERT_EQ_INT(VALUE_str, item_value(msg_item)->type);
  ASSERT_TRUE(item_value(msg_item)->s != NULL && strlen(item_value(msg_item)->s) > 0);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "SIN-PARSE-") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "stage=PARSE") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "file=<memory>") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "line=") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "column=") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "message=") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "excerpt=sys.log{;") != NULL);
  ITEM_t *error_item = find_item(itemstore_root(config.itemstore_ctx), "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_nil, item_value(error_item)->type);
  assert_string_item("error.code", "SIN-PARSE-");
  assert_string_item("error.stage", "PARSE");
  assert_string_item("error.file", "<memory>");
  assert_int_item("error.line", 1);
  assert_int_item("error.column", 9);
  assert_string_item("error.excerpt", "sys.log{;");

  VALUE_t intarg = {VALUE_int, {.i = 42}};
  push_stack(config.vm->stack, intarg);
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err_item)->i);
  msg_item = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg_item);
  ASSERT_EQ_INT(VALUE_str, item_value(msg_item)->type);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "sys.compile") != NULL);
  ASSERT_TRUE(strstr(item_value(msg_item)->s, "string") != NULL);

  int32_t baseline = config.vm->stack->current;
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  push_stack(config.vm->stack, vstr("sys.exists{42};"));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err_item)->i);
  msg_item = assert_string_item("error.msg", "sys.exists");
  ASSERT_NOT_NULL(msg_item);
  error_item = assert_string_item("error.item", "__sys_compile_tmp__");
  char failed_tmp_name[MAX_ITEM_NAME];
  int written = snprintf(failed_tmp_name, sizeof(failed_tmp_name), "%s",
                         item_value(error_item)->s);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(failed_tmp_name));
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), failed_tmp_name) == NULL);
  ASSERT_EQ_INT(baseline, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  assert_only_temp_item_named(NULL);

  uint64_t failed_counter = parse_temp_counter(failed_tmp_name);
  uint64_t collision_counter = failed_counter == UINT64_MAX
      ? UINT64_C(0) : failed_counter + 1u;
  char collision_name[MAX_ITEM_NAME];
  written = snprintf(collision_name, sizeof(collision_name),
                     "__sys_compile_tmp__%llu",
                     (unsigned long long)collision_counter);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(collision_name));
  char collision_child_name[MAX_ITEM_NAME];
  written = snprintf(collision_child_name, sizeof(collision_child_name),
                     "%s.child", collision_name);
  ASSERT_TRUE(written > 0 &&
              (size_t)written < sizeof(collision_child_name));
  ASSERT_NOT_NULL(test_item_set_value(itemstore_root(config.itemstore_ctx), collision_name,
                              (VALUE_t){VALUE_int, {.i = 777}}));
  ASSERT_NOT_NULL(test_item_set_value(itemstore_root(config.itemstore_ctx), collision_child_name,
                              (VALUE_t){VALUE_int, {.i = 888}}));

  assert_compile_success_bool("collision_probe = true;");
  assert_int_item(collision_name, 777);
  assert_int_item(collision_child_name, 888);
  assert_only_temp_item_named(collision_name);

  push_stack(config.vm->stack,
             vstr("sys.exists{42}; error = nil; compiled_value = 91;"));
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 1);
  assert_int_item("compiled_value", 91);
  assert_error_fields_nil();
  ASSERT_EQ_INT(baseline, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  assert_only_temp_item_named(collision_name);

  volatile sig_atomic_t interrupt_pending = 0;
  RuntimeContext *interrupt_ctx = test_ctx();
  interrupt_ctx->interrupt_pending = &interrupt_pending;
  ASSERT_TRUE(runtime_init(interrupt_ctx, config.vm));
  interrupt_ctx->opcode[(uint8_t)'b'] = inject_interrupt_after_push_bool;
  push_stack(config.vm->stack, vstr("true;"));
  (void)lc_sys_compile(interrupt_ctx, NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  ASSERT_TRUE(interrupt_ctx->interrupted);
  ASSERT_EQ_INT(0, interrupt_pending);
  err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_SIGUSR1, item_value(err_item)->i);
  ASSERT_EQ_INT(baseline, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  // The only prefix-matching item is the pre-existing collision sentinel;
  // the interrupted execution's temporary item has been deleted.
  assert_only_temp_item_named(collision_name);
  runtime_destroy(interrupt_ctx);

  for (int i = 0; i < 50; i++) {
    push_stack(config.vm->stack, vstr("sys.log{\"x\\n\"};"));
    (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
    assert_bool(pop_stack(config.vm->stack), 1);
    ASSERT_EQ_INT(baseline, config.vm->stack->current);
    ASSERT_EQ_INT(-1, config.vm->callstack->current);
    assert_only_temp_item_named(collision_name);
  }

  size_t deep_len = 5000u * 2u + 2u;
  char *deep_source = malloc(deep_len);
  ASSERT_NOT_NULL(deep_source);
  size_t pos = 0;
  for (int i = 0; i < 5000; ++i) {
    deep_source[pos++] = '1';
    if (i != 4999) deep_source[pos++] = '+';
  }
  deep_source[pos++] = ';'; deep_source[pos] = '\0';
  push_stack(config.vm->stack, vstr(deep_source));
  free(deep_source);
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  ASSERT_EQ_INT(baseline, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  assert_string_item("error.msg", "AST traversal depth budget exceeded");
  assert_only_temp_item_named(collision_name);
  assert_compile_success_bool("recovery_value = 123;");
  assert_int_item("recovery_value", 123);

  char shallow_source[256];
  size_t shallow_pos = 0;
  for (int i = 0; i < 40; ++i) {
    memcpy(shallow_source + shallow_pos, "1;", 2);
    shallow_pos += 2;
  }
  shallow_source[shallow_pos] = '\0';
  push_stack(config.vm->stack, vstr(shallow_source));
  RuntimeContext *limited_ctx = test_ctx();
  limited_ctx->compiler_ast_node_limit = 32;
  (void)lc_sys_compile(limited_ctx, NULL, itemstore_root(config.itemstore_ctx));
  assert_bool(pop_stack(config.vm->stack), 0);
  assert_string_item("error.msg", "AST node budget exceeded");
  assert_compile_success_bool("node_budget_recovery = 456;");
  assert_int_item("node_budget_recovery", 456);

  teardown_runtime();
}
