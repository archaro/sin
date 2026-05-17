#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "ir.h"
#include "semant.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef enum {
  STAGE_PARSER,
  STAGE_SEMANTIC,
  STAGE_LOWER_IR_VALIDATE,
  STAGE_EMITTER,
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
} NEG_CASE;

static int8_t run_emit_case_invalid_label(char **errdetail) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = 42});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;

  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, errdetail);

  free(out.bytecode);
  ir_destroy_unit(unit);
  return rc;
}

static int8_t run_emit_case_unsupported_op(char **errdetail) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = (IR_Op)999});

  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;

  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, errdetail);

  free(out.bytecode);
  ir_destroy_unit(unit);
  return rc;
}

static int8_t run_ir_case_bad_local_index(char **errdetail) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_INC_LOCAL, .a = 3});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  int8_t rc = ir_validate(unit, 1, errdetail);
  ir_destroy_unit(unit);
  return rc;
}

static int8_t run_ir_case_bad_arity(char **errdetail) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_CALL, .a = -1, .b = 0});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  int8_t rc = ir_validate(unit, 0, errdetail);
  ir_destroy_unit(unit);
  return rc;
}

void test_pipeline_negative_matrix(void) {
  static const NEG_CASE cases[] = {
      {"parser_unknown_char", CASE_SOURCE, "^;", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "^"},
      {"parser_malformed_item_syntax", CASE_SOURCE, "foo..bar;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error"},
      {"parser_unterminated_string", CASE_SOURCE, "\"unterminated;", NULL, ERR_COMP_UNKNOWNCHAR, STAGE_PARSER, "Unterminated string literal."},
      {"parser_bad_if_endif_pairing", CASE_SOURCE, "endif;", NULL, ERR_COMP_SYNTAX, STAGE_PARSER, "syntax error"},

      {"semantic_use_before_def", CASE_SOURCE, "@x;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @x"},
      {"semantic_invalid_increment_target", CASE_SOURCE, "@x = 1; @y++;", NULL, ERR_COMP_LOCALBEFOREDEF, STAGE_SEMANTIC, "semant: @y"},

      {"ir_validate_local_index_bounds", CASE_BUILDER, NULL, run_ir_case_bad_local_index, ERR_COMP_LOCALBEFOREDEF, STAGE_LOWER_IR_VALIDATE, "ir: Instruction 0 (INC_LOCAL) has out-of-range local index"},
      {"ir_validate_arity_constraint", CASE_BUILDER, NULL, run_ir_case_bad_arity, ERR_COMP_TOOMANYARGS, STAGE_LOWER_IR_VALIDATE, "ir: Instruction 0 (CALL) has negative arity"},

      {"emitter_invalid_label", CASE_BUILDER, NULL, run_emit_case_invalid_label, ERR_COMP_SYNTAX, STAGE_EMITTER, "emitbc: jump invalid label id"},
      {"emitter_unsupported_op", CASE_BUILDER, NULL, run_emit_case_unsupported_op, ERR_COMP_SYNTAX, STAGE_EMITTER, "emitbc: unsupported IR op"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const NEG_CASE *tc = &cases[i];
    char *errdetail = NULL;
    int8_t rc = ERR_NOERROR;

    if (tc->source_or_builder == CASE_SOURCE) {
      OUTPUT_t *out = NULL;
      rc = compile_source_to_bytecode(tc->source, strlen(tc->source), &out, &errdetail);
      ASSERT_TRUE(out == NULL);
    } else {
      rc = tc->builder(&errdetail);
    }

    ASSERT_EQ_INT(tc->expected_code, rc);
    ASSERT_NOT_NULL(errdetail);
    ASSERT_TRUE(strstr(errdetail, tc->expected_substring) != NULL);

    switch (tc->expected_stage) {
      case STAGE_PARSER:
        ASSERT_TRUE(strncmp(tc->name, "parser_", 7) == 0);
        break;
      case STAGE_SEMANTIC:
        ASSERT_TRUE(strncmp(tc->name, "semantic_", 9) == 0);
        break;
      case STAGE_LOWER_IR_VALIDATE:
        ASSERT_TRUE(strncmp(tc->name, "ir_validate_", 12) == 0);
        break;
      case STAGE_EMITTER:
        ASSERT_TRUE(strncmp(tc->name, "emitter_", 8) == 0);
        break;
    }

    free(errdetail);
  }
}
