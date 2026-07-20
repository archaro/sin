#include <errno.h>
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
#include "version.h"
#include "vm.h"

CONFIG_t config;
static RuntimeContext test_runtime_ctx;
static RuntimeContext *test_ctx(void) {
  runtime_context_init(&test_runtime_ctx, config.vm);
  test_runtime_ctx.itemroot = config.itemroot;
  test_runtime_ctx.strict_validation = config.strict_validation;
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
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}

static void assert_bool(VALUE_t v, int expected) {
  ASSERT_EQ_INT(VALUE_bool, v.type);
  ASSERT_EQ_INT(expected, v.i);
}

static ITEM_t *assert_string_item(const char *name, const char *contains) {
  ITEM_t *item = find_item(config.itemroot, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_str, item->value.type);
  ASSERT_NOT_NULL(item->value.s);
  if (contains) {
    ASSERT_TRUE(strstr(item->value.s, contains) != NULL);
  }
  return item;
}

static ITEM_t *assert_int_item(const char *name, int64_t expected) {
  ITEM_t *item = find_item(config.itemroot, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_int, item->value.type);
  ASSERT_EQ_INT(expected, item->value.i);
  return item;
}

static ITEM_t *assert_nil_item(const char *name) {
  ITEM_t *item = find_item(config.itemroot, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_value, item->type);
  ASSERT_EQ_INT(VALUE_nil, item->value.type);
  return item;
}

static void assert_compile_success_bool(const char *source) {
  push_stack(config.vm->stack, vstr(source));
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 1);
}

static void assert_error_fields_nil(void) {
  static const char *const names[] = {
    "error", "error.msg", "error.item", "error.code", "error.stage",
    "error.file", "error.line", "error.column", "error.excerpt"
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    ITEM_t *field = find_item(config.itemroot, names[i]);
    ASSERT_NOT_NULL(field);
    ASSERT_EQ_INT(VALUE_nil, field->value.type);
  }
}

static void assert_only_temp_item_named(const char *expected_name) {
  const char *prefix = "__sys_compile_tmp__";
  size_t prefix_len = strlen(prefix);
  size_t found = 0;
  for (size_t i = 0; i < config.itemroot->ordered_size; i++) {
    ITEM_t *child = config.itemroot->ordered_array[i];
    if (strncmp(child->name, prefix, prefix_len) == 0) {
      ASSERT_NOT_NULL(expected_name);
      ASSERT_TRUE(strcmp(child->name, expected_name) == 0);
      found++;
    }
  }
  ASSERT_EQ_INT(expected_name ? 1 : 0, found);
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
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
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
  ASSERT_EQ_INT(ITEM_value, foo->type);
  assert_compile_success_bool("syscompile.observed = foo;");
  assert_int_item("syscompile.observed", 42);

  assert_compile_success_bool("foo = code {@in} ( @in+10; );");
  foo = find_item(config.itemroot, "foo");
  ASSERT_NOT_NULL(foo);
  ASSERT_EQ_INT(ITEM_code, foo->type);
  ASSERT_NOT_NULL(foo->bytecode);
  ASSERT_TRUE(foo->bytecode_len >= 3u);
  ASSERT_EQ_INT(1, foo->bytecode[1]);
  assert_compile_success_bool("syscompile.observed = foo{10};");
  assert_int_item("syscompile.observed", 20);

  assert_compile_stdout("sys.log{\"Hello\\n\"};", "Hello\n");
  assert_only_temp_item_named(NULL);

  set_error_item(config.itemroot, ERR_RUNTIME_INVALIDARGS, "stale error",
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
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection.callee") == 0);
  introspection_value = assert_string_item(
      "introspection.results.callee_parent", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_before", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection.caller") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_after", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection.caller") == 0);
  introspection_value = assert_string_item(
      "introspection.results.caller_parent", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection") == 0);
  introspection_value = assert_string_item(
      "introspection.results.top_this", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "introspection_top") == 0);
  assert_nil_item("introspection.results.top_parent");
  introspection_value = assert_string_item(
      "introspection.results.item_type", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "code") == 0);
  ITEM_t *introspection_count = find_item(
      config.itemroot, "introspection.results.child_count");
  ASSERT_NOT_NULL(introspection_count);
  ASSERT_EQ_INT(VALUE_int, introspection_count->value.type);
  ASSERT_TRUE(introspection_count->value.i >= 3);
  introspection_count = find_item(config.itemroot,
                                  "introspection.results.root_count");
  ASSERT_NOT_NULL(introspection_count);
  ASSERT_EQ_INT(VALUE_int, introspection_count->value.type);
  ASSERT_TRUE(introspection_count->value.i > 0);
  introspection_value = assert_string_item(
      "introspection.results.version", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, SINVERSION) == 0);
  ITEM_t *introspection_time = find_item(config.itemroot,
                                         "introspection.results.now");
  ASSERT_NOT_NULL(introspection_time);
  ASSERT_EQ_INT(VALUE_int, introspection_time->value.type);
  ASSERT_TRUE(introspection_time->value.i > 0);
  introspection_time = find_item(config.itemroot,
                                 "introspection.results.monotime");
  ASSERT_NOT_NULL(introspection_time);
  ASSERT_EQ_INT(VALUE_int, introspection_time->value.type);
  ASSERT_TRUE(introspection_time->value.i >= 0);

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
      " caller.results.compile_host_before = sys.calleritem;"
      " sys.compile{\"caller.results.compile_temp = sys.calleritem;"
      " caller.compile_target;\"};"
      " caller.results.compile_host_after = sys.calleritem;"
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
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.a") == 0);
  introspection_value = assert_string_item("caller.results.c", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.b") == 0);
  introspection_value = assert_string_item("caller.results.b_after", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.a") == 0);
  introspection_value = assert_string_item("caller.results.a_before",
                                            "__sys_compile_tmp__");
  char first_temp_caller[MAX_ITEM_NAME];
  int caller_written = snprintf(first_temp_caller, sizeof(first_temp_caller),
                                "%s", introspection_value->value.s);
  ASSERT_TRUE(caller_written > 0 &&
              (size_t)caller_written < sizeof(first_temp_caller));
  introspection_value = assert_string_item("caller.results.a_after", NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, first_temp_caller) == 0);
  assert_int_item("caller.results.zero_params", 0);
  assert_int_item("caller.results.multiple_params", 3);

  ITEM_t *compile_outer = find_item(config.itemroot, "caller.compile_outer");
  ASSERT_NOT_NULL(compile_outer);
  RuntimeContext *caller_ctx = test_ctx();
  caller_ctx->invocation_callstack_floor = 29;
  caller_ctx->invocation_caller_item = foo;
  VALUE_t caller_result = interpret(caller_ctx, compile_outer);
  ASSERT_EQ_INT(VALUE_nil, caller_result.type);
  ASSERT_EQ_INT(29, caller_ctx->invocation_callstack_floor);
  ASSERT_TRUE(caller_ctx->invocation_caller_item == foo);
  introspection_value = assert_string_item("caller.results.compile_host_before",
                                            NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.compile_outer") ==
              0);
  introspection_value = assert_string_item("caller.results.compile_temp",
                                            NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.compile_host") ==
              0);
  introspection_value = assert_string_item("caller.results.compile_target",
                                            "__sys_compile_tmp__");
  introspection_value = assert_string_item("caller.results.compile_host_after",
                                            NULL);
  ASSERT_TRUE(strcmp(introspection_value->value.s, "caller.compile_outer") ==
              0);
  assert_nil_item("caller.results.compile_outer_after");

  char source_srcroot[] = "/tmp/sin-sys-source-compile-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(source_srcroot));
  config.srcroot = source_srcroot;
  assert_compile_success_bool("source_runtime.target = code ( 7; );");
  ITEM_t *source_target = find_item(config.itemroot, "source_runtime.target");
  ASSERT_NOT_NULL(source_target);
  char compiled_source[] = "source_runtime.target = code ( 7; );\n";
  ASSERT_TRUE(save_itemsource_in_srcroot(source_target, compiled_source,
                                         source_srcroot));
  assert_compile_success_bool(
      "source_runtime.result = sys.source{\"source_runtime.target\"};");
  ITEM_t *source_result = assert_string_item("source_runtime.result", NULL);
  ASSERT_TRUE(strcmp(source_result->value.s, compiled_source) == 0);
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
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  ITEM_t *err_item = find_item(config.itemroot, "error");
  ITEM_t *msg_item = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(err_item);
  ASSERT_NOT_NULL(msg_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_TRUE(err_item->value.i != ERR_NOERROR);
  ASSERT_EQ_INT(VALUE_str, msg_item->value.type);
  ASSERT_TRUE(msg_item->value.s != NULL && strlen(msg_item->value.s) > 0);
  ASSERT_TRUE(strstr(msg_item->value.s, "SIN-PARSE-") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "stage=PARSE") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "file=<memory>") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "line=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "column=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "message=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "excerpt=sys.log{;") != NULL);
  ITEM_t *error_item = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_nil, error_item->value.type);
  assert_string_item("error.code", "SIN-PARSE-");
  assert_string_item("error.stage", "PARSE");
  assert_string_item("error.file", "<memory>");
  assert_int_item("error.line", 1);
  assert_int_item("error.column", 9);
  assert_string_item("error.excerpt", "sys.log{;");

  VALUE_t intarg = {VALUE_int, {.i = 42}};
  push_stack(config.vm->stack, intarg);
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err_item->value.i);
  msg_item = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg_item);
  ASSERT_EQ_INT(VALUE_str, msg_item->value.type);
  ASSERT_TRUE(strstr(msg_item->value.s, "sys.compile") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "string") != NULL);

  int32_t baseline = config.vm->stack->current;
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  push_stack(config.vm->stack, vstr("sys.exists{42};"));
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err_item->value.i);
  msg_item = assert_string_item("error.msg", "sys.exists");
  ASSERT_NOT_NULL(msg_item);
  error_item = assert_string_item("error.item", "__sys_compile_tmp__");
  char failed_tmp_name[MAX_ITEM_NAME];
  int written = snprintf(failed_tmp_name, sizeof(failed_tmp_name), "%s",
                         error_item->value.s);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(failed_tmp_name));
  ASSERT_TRUE(find_item(config.itemroot, failed_tmp_name) == NULL);
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
  ASSERT_NOT_NULL(insert_item(config.itemroot, collision_name,
                              (VALUE_t){VALUE_int, {.i = 777}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, collision_child_name,
                              (VALUE_t){VALUE_int, {.i = 888}}));

  assert_compile_success_bool("collision_probe = true;");
  assert_int_item(collision_name, 777);
  assert_int_item(collision_child_name, 888);
  assert_only_temp_item_named(collision_name);

  push_stack(config.vm->stack,
             vstr("sys.exists{42}; error = nil; compiled_value = 91;"));
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
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
  (void)lc_sys_compile(interrupt_ctx, NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  ASSERT_TRUE(interrupt_ctx->interrupted);
  ASSERT_EQ_INT(0, interrupt_pending);
  err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_SIGUSR1, err_item->value.i);
  ASSERT_EQ_INT(baseline, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  // The only prefix-matching item is the pre-existing collision sentinel;
  // the interrupted execution's temporary item has been deleted.
  assert_only_temp_item_named(collision_name);
  runtime_destroy(interrupt_ctx);

  for (int i = 0; i < 50; i++) {
    push_stack(config.vm->stack, vstr("sys.log{\"x\\n\"};"));
    (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
    assert_bool(pop_stack(config.vm->stack), 1);
    ASSERT_EQ_INT(baseline, config.vm->stack->current);
    ASSERT_EQ_INT(-1, config.vm->callstack->current);
    assert_only_temp_item_named(collision_name);
  }

  teardown_runtime();
}
