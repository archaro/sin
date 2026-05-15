#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "lower.h"
#include "parser.h"
#include "semant.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *source;
  const char *fixture_path;
} SourceGoldenCase;

static void run_source_case(const SourceGoldenCase *tc) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  int8_t rc = parse_source((char *)tc->source, (int)strlen(tc->source), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);

  rc = sem_check_locals(absyn, &errdetail, sem);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  IR_Unit *ir = NULL;
  rc = lower_ast_to_ir(absyn, sem, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);

  rc = ir_validate(ir, sem->count, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  OUTPUT_t out = {0};
  out.maxsize = 128;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  rc = t_emit_bytecode(ir, (uint8_t)sem->count, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(tc->fixture_path, &expected_len);
  size_t actual_len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);
  ASSERT_EQ_INT(0, memcmp(expected, out.bytecode, expected_len));

  free(expected);
  free(out.bytecode);
  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(absyn);
}

static void test_source_pipeline_negative_cases(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;

  const char *bad_char = "^;";
  int8_t rc = parse_source((char *)bad_char, (int)strlen(bad_char), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "^") == 0);
  free(errdetail);

  const char *bad_semantic = "@x;";
  rc = parse_source((char *)bad_semantic, (int)strlen(bad_semantic), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  rc = sem_check_locals(absyn, &errdetail, sem);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "x") != NULL);

  free(errdetail);
  sem_delete_ctx(sem);
  as_delete(absyn);

  absyn = NULL;
  errdetail = NULL;
  const char *bad_inc_semantic = "@y++;";
  rc = parse_source((char *)bad_inc_semantic, (int)strlen(bad_inc_semantic), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);

  sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  rc = sem_check_locals(absyn, &errdetail, sem);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "y") != NULL);

  free(errdetail);
  sem_delete_ctx(sem);
  as_delete(absyn);
}


static void test_source_exprstmt_libcall_no_pop(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  const char *source = "sys.log{\"hello\"};";

  int8_t rc = parse_source((char *)source, (int)strlen(source), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  rc = sem_check_locals(absyn, &errdetail, sem);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  IR_Unit *ir = NULL;
  rc = lower_ast_to_ir(absyn, sem, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  OUTPUT_t out = {0};
  out.maxsize = 128;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  rc = t_emit_bytecode(ir, (uint8_t)sem->count, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  static const uint8_t expected[] = {
      0x00, 0x00,
      0x6c, 0x05, 0x00, 'h', 'e', 'l', 'l', 'o',
      0x6c, 0x03, 0x00, 's', 'y', 's',
      0x6c, 0x03, 0x00, 'l', 'o', 'g',
      0x41, 0x01,
      0x68,
  };
  size_t n = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT((int)sizeof(expected), (int)n);
  ASSERT_EQ_INT(0, memcmp(expected, out.bytecode, n));

  free(out.bytecode);
  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(absyn);
}

static void test_source_item_with_numeric_layer(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  const char *source = "foo.12;";

  int8_t rc = parse_source((char *)source, (int)strlen(source), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  rc = sem_check_locals(absyn, &errdetail, sem);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  IR_Unit *ir = NULL;
  rc = lower_ast_to_ir(absyn, sem, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);

  OUTPUT_t out = {0};
  out.maxsize = 128;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  rc = t_emit_bytecode(ir, (uint8_t)sem->count, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  static const uint8_t expected[] = {
      0x00, 0x00,
      'I',
      'L', 0x03, 'f', 'o', 'o',
      'L', 0x02, '1', '2',
      'E',
      'h',
  };
  size_t n = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT((int)sizeof(expected), (int)n);
  ASSERT_EQ_INT(0, memcmp(expected, out.bytecode, n));

  free(out.bytecode);
  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(absyn);
}

void test_pipeline_source_golden(void) {
  const SourceGoldenCase cases[] = {
      {"int_literal", "42;", "tests/fixtures/int_literal.hex"},
      {"locals_store_load", "@x = 7; @x;", "tests/fixtures/locals_store_load.hex"},
      {"arithmetic_add", "2 + 3;", "tests/fixtures/arithmetic_add.hex"},
      {"if_elsif_else", "if 1 < 2 then 9; elsif 0 < 1 then 8; else 7; endif;", "tests/fixtures/if_elsif_else.hex"},
      {"locals_inc", "@x = 1; @x++; @x;", "tests/fixtures/locals_inc.hex"},
      {"locals_dec", "@x = 2; @x--; @x;", "tests/fixtures/locals_dec.hex"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_source_case(&cases[i]);
  }

  test_source_pipeline_negative_cases();
  test_source_exprstmt_libcall_no_pop();
  test_source_item_with_numeric_layer();
}
