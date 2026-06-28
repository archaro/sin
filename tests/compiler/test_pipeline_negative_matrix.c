#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"

typedef enum {
  STAGE_PARSER,
  STAGE_SEMANTIC,
} NEG_STAGE;

typedef enum {
  CASE_SOURCE,
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

void test_pipeline_negative_matrix(void) {
  static const NEG_CASE cases[] = {
      {"parser_unknown_char", CASE_SOURCE, "^;", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "^", 1},
      {"parser_malformed_item_syntax", CASE_SOURCE, "foo..bar;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_unterminated_string", CASE_SOURCE, "\"unterminated;", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "Unterminated string literal.", 1},
      {"parser_bad_if_endif_pairing", CASE_SOURCE, "endif;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},
      {"parser_return_keyword_edge", CASE_SOURCE, "return @l == false;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 1},

      {"semantic_use_before_def", CASE_SOURCE, "@x;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @x", 1},
      {"semantic_invalid_increment_target", CASE_SOURCE, "@x = 1; @y++;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @y", 1},

      {"parser_combo_priority_over_semantic", CASE_SOURCE, "@x; endif;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error", 3},
      {"semantic_combo_first_undefined_local", CASE_SOURCE, "@b; @a++;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @b", 3},

      {"semantic_boolean_local_roundtrip", CASE_SOURCE, "@l = true; @l == false;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_boolean_if_clause", CASE_SOURCE, "@l = false; if @l == false then sys.log{\"False\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_truthiness_int_unchanged", CASE_SOURCE, "if 1 then sys.log{\"t\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},
      {"semantic_truthiness_empty_string_unchanged", CASE_SOURCE, "if \"\" then sys.log{\"t\"}; endif;", NULL, ERR_NOERROR, STAGE_SEMANTIC, NULL, 1},

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
      if (tc->source_or_builder == CASE_SOURCE) {
        rc = compile_source_to_bytecode(tc->source, strlen(tc->source), &out, &errdetail);
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
