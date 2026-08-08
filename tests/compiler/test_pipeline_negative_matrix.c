#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef enum {
  STAGE_PARSER,
  STAGE_SEMANTIC,
} NEG_STAGE;

typedef enum {
  CASE_SOURCE,
  CASE_FIXTURE,
  CASE_BUILDER,
} CASE_KIND;

typedef int8_t (*builder_fn)(char **errdetail);

typedef struct {
  const char *name;
  CASE_KIND source_or_builder;
  const char *source;
  builder_fn builder;
  int8_t expected_code;
  NEG_STAGE expected_stage;
  const char *expected_substring;
  uint8_t deterministic_runs;
} NEG_CASE;

static int8_t run_compile_case_too_many_params(char **errdetail) {
  const size_t param_count = UINT8_MAX + 1u;
  const char **params = calloc(param_count, sizeof(char *));
  char **owned = calloc(param_count, sizeof(char *));
  ASSERT_NOT_NULL(params);
  ASSERT_NOT_NULL(owned);
  for (size_t i = 0; i < param_count; i++) {
    owned[i] = malloc(16);
    ASSERT_NOT_NULL(owned[i]);
    snprintf(owned[i], 16, "p%zu", i);
    params[i] = owned[i];
  }

  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode_with_params("1;", 2, params, param_count, &out, errdetail);
  ASSERT_TRUE(out == NULL);
  for (size_t i = 0; i < param_count; i++) free(owned[i]);
  free(owned);
  free(params);
  return rc;
}

static int8_t run_compile_case_libcall_256_args(char **errdetail) {
  const size_t arg_count = (size_t)UINT8_MAX + 1u;
  const size_t source_len = strlen("sys.backup{");
  const size_t suffix_len = strlen("};");
  char *source = malloc(source_len + arg_count * 2u + suffix_len + 1u);
  ASSERT_NOT_NULL(source);
  size_t offset = source_len;
  memcpy(source, "sys.backup{", source_len);
  for (size_t i = 0; i < arg_count; i++) {
    source[offset++] = '1';
    source[offset++] = (i + 1u == arg_count) ? '}' : ',';
  }
  source[offset++] = ';';
  source[offset] = '\0';

  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode(source, offset, &out, errdetail);
  ASSERT_TRUE(out == NULL);
  free(source);
  return rc;
}

void test_pipeline_negative_matrix(void) {
  static const NEG_CASE cases[] = {
      {"parser_unknown_char", CASE_FIXTURE, "tests/fixtures/conformance/negative/parser-unknown-character.src", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "^", 1},
      {"parser_nul_escape", CASE_FIXTURE, "tests/fixtures/conformance/negative/parser-nul-escape.src", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "NUL byte escape", 1},
      {"parser_trailing_comma", CASE_FIXTURE, "tests/fixtures/conformance/negative/parser-trailing-comma.src", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_integer_overflow", CASE_FIXTURE, "tests/fixtures/conformance/negative/parser-integer-overflow.src", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "integer literal out of range", 1},
      {"parser_foreach_nonlocal_iterator", CASE_FIXTURE, "tests/fixtures/conformance/negative/parser-foreach-nonlocal-iterator.src", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_malformed_item_syntax", CASE_SOURCE, "foo..bar;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_unterminated_string", CASE_SOURCE, "\"unterminated;", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "EOF in string.", 1},
      {"parser_bad_if_endif_pairing", CASE_SOURCE, "endif;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_do_while_missing_condition", CASE_SOURCE, "do 1; while ;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 3},
      {"semantic_use_before_def", CASE_FIXTURE, "tests/fixtures/conformance/negative/semantic-local-before-definition.src", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @x", 1},
      {"semantic_invalid_increment_target", CASE_SOURCE, "@x = 1; @y++;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @y", 1},
      {"semantic_break_outside_loop", CASE_FIXTURE, "tests/fixtures/conformance/negative/semantic-break-outside-loop.src", NULL, ERR_COMP_SYNTAX, STAGE_SEMANTIC, "BREAK outside loop", 2},
      {"semantic_continue_outside_loop", CASE_SOURCE, "continue;", NULL, ERR_COMP_SYNTAX, STAGE_SEMANTIC, "CONTINUE outside loop", 2},

      {"parser_combo_priority_over_semantic", CASE_SOURCE, "@x; endif;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 3},
      {"semantic_combo_first_undefined_local", CASE_SOURCE, "@b; @a++;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @b", 3},

      {"semantic_boolean_local_roundtrip", CASE_SOURCE, "@l = true; @l == false;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_boolean_if_clause", CASE_SOURCE, "@l = false; if @l == false then sys.log{\"False\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_truthiness_int_unchanged", CASE_SOURCE, "if 1 then sys.log{\"t\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_truthiness_empty_string_unchanged", CASE_SOURCE, "if \"\" then sys.log{\"t\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_do_while_body_defines_condition_local", CASE_SOURCE, "do @x = 1; while @x < 2; @x;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},

      {"semantic_libcall_zero_args", CASE_SOURCE, "sys.backup;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_libcall_256_args", CASE_BUILDER, NULL, run_compile_case_libcall_256_args, ERR_COMP_SYNTAX, STAGE_SEMANTIC, "invalid libcall argument count", 3},

      {"semantic_compile_param_count_guard", CASE_BUILDER, NULL, run_compile_case_too_many_params, ERR_COMP_TOOMANYLOCALS, STAGE_SEMANTIC, "", 1},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const NEG_CASE *tc = &cases[i];
    char *errdetail = NULL;
    OUTPUT_t *out = NULL;
    int8_t rc = ERR_NOERROR;

    uint8_t runs = tc->deterministic_runs ? tc->deterministic_runs : 1;
    char *baseline_errdetail = NULL;
    for (uint8_t run = 0; run < runs; run++) {
      if (tc->source_or_builder == CASE_SOURCE || tc->source_or_builder == CASE_FIXTURE) {
        char *fixture_source = NULL;
        const char *source = tc->source;
        if (tc->source_or_builder == CASE_FIXTURE) {
          fixture_source = test_read_text_file(tc->source);
          ASSERT_NOT_NULL(fixture_source);
          source = fixture_source;
        }
        rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
        free(fixture_source);
        if (tc->expected_code == ERR_NOERROR) {
          ASSERT_NOT_NULL(out);
        } else {
          ASSERT_TRUE(out == NULL);
        }
      } else {
        rc = tc->builder(&errdetail);
      }

      ASSERT_EQ_INT(tc->expected_code, rc);
      if (tc->expected_code == ERR_NOERROR) {
        ASSERT_TRUE(errdetail == NULL);
      } else {
        ASSERT_NOT_NULL(errdetail);
        ASSERT_TRUE(strstr(errdetail, tc->expected_substring) != NULL);
        if (baseline_errdetail == NULL) {
          baseline_errdetail = strdup(errdetail);
          ASSERT_NOT_NULL(baseline_errdetail);
        } else {
          ASSERT_TRUE(strcmp(baseline_errdetail, errdetail) == 0);
        }
      }

      if (out) {
        free(out->bytecode);
        free(out);
        out = NULL;
      }
      if (errdetail) {
        free(errdetail);
        errdetail = NULL;
      }
    }
    if (baseline_errdetail) free(baseline_errdetail);

    switch (tc->expected_stage) {
      case STAGE_PARSER:
        ASSERT_TRUE(strncmp(tc->name, "parser_", 7) == 0);
        break;
      case STAGE_SEMANTIC:
        ASSERT_TRUE(strncmp(tc->name, "semantic_", 9) == 0);
        break;
    }

  }
}

void test_pipeline_ast_budget_subprocess(void) {
  char srcname[] = "/tmp/sin-ast-src-XXXXXX";
  char outname[] = "/tmp/sin-ast-out-XXXXXX";
  int sfd = mkstemp(srcname), ofd = mkstemp(outname);
  ASSERT_TRUE(sfd >= 0 && ofd >= 0);
  FILE *f = fdopen(sfd, "w"); ASSERT_NOT_NULL(f);
  for (int i = 0; i < 5000; ++i) fprintf(f, "%s1", i ? "+" : "");
  fputs(";\n", f); fclose(f); close(ofd);
  char cmd[512]; snprintf(cmd, sizeof(cmd), "./scomp -q -i %s -o %s > /tmp/sin-ast-log 2>&1", srcname, outname);
  int status = system(cmd);
  ASSERT_TRUE(WIFEXITED(status)); ASSERT_TRUE(WEXITSTATUS(status) != 0);
  FILE *log = fopen("/tmp/sin-ast-log", "r"); ASSERT_NOT_NULL(log);
  char text[2048] = {0}; size_t got = fread(text, 1, sizeof(text) - 1, log); (void)got; fclose(log);
  ASSERT_TRUE(strstr(text, "AST traversal depth budget exceeded") != NULL);
  unlink(srcname); unlink(outname); unlink("/tmp/sin-ast-log");

  char shallow[256];
  size_t shallow_len = 0;
  for (int i = 0; i < 40; ++i) {
    memcpy(shallow + shallow_len, "1;", 2);
    shallow_len += 2;
  }
  shallow[shallow_len] = '\0';
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  ParseInput shallow_input = {shallow, shallow_len, "node-budget.sin"};
  CompilerDiagnostic shallow_diag;
  compiler_diag_init(&shallow_diag);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX,
                compile_parse_input_to_bytecode_diag_with_node_limit(
                    &shallow_input, 32, &out, &shallow_diag));
  ASSERT_TRUE(out == NULL);
  ASSERT_NOT_NULL(shallow_diag.message);
  ASSERT_TRUE(strstr(shallow_diag.message, "AST node budget exceeded") != NULL);
  compiler_diag_reset(&shallow_diag);

  char shallow_srcname[] = "/tmp/sin-ast-shallow-src-XXXXXX";
  char shallow_outname[] = "/tmp/sin-ast-shallow-out-XXXXXX";
  sfd = mkstemp(shallow_srcname);
  ofd = mkstemp(shallow_outname);
  ASSERT_TRUE(sfd >= 0 && ofd >= 0);
  f = fdopen(sfd, "w");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT((int)shallow_len, (int)fwrite(shallow, 1, shallow_len, f));
  fclose(f);
  close(ofd);
  ASSERT_EQ_INT(0, setenv("SINISTRA_TEST_AST_NODE_LIMIT", "32", 1));
  snprintf(cmd, sizeof(cmd),
           "./scomp -q -i %s -o %s > /tmp/sin-ast-log 2>&1", shallow_srcname,
           shallow_outname);
  status = system(cmd);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  log = fopen("/tmp/sin-ast-log", "r");
  ASSERT_NOT_NULL(log);
  memset(text, 0, sizeof(text));
  got = fread(text, 1, sizeof(text) - 1, log);
  (void)got;
  fclose(log);
  ASSERT_TRUE(strstr(text, "AST node budget exceeded") != NULL);
  ASSERT_EQ_INT(0, unsetenv("SINISTRA_TEST_AST_NODE_LIMIT"));
  unlink(shallow_srcname);
  unlink(shallow_outname);
  unlink("/tmp/sin-ast-log");

  ASSERT_EQ_INT(ERR_NOERROR,
                compile_source_to_bytecode("1;", 2, &out, &errdetail));
  ASSERT_NOT_NULL(out);
  free(out->bytecode);
  free(out);
}
