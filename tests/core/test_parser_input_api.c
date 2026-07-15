#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/absyn.h"
#include "compiler/compdiag.h"
#include "error.h"
#include "compiler/parse_input.h"
#include "parser.h"
#include "string_limits.h"
#include "test_assert.h"

void test_parser_input_api(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;

  const char malformed[] = "@x = ;";
  ParseInput malformed_input = {malformed, sizeof(malformed) - 1, "malformed.src"};
  int8_t rc = parse_source(&malformed_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(errdetail != NULL);
  free(errdetail);

  const char embedded_nul[] = {'@','x','=','1',';','\0','@','y','=','2',';'};
  ParseInput nul_input = {embedded_nul, sizeof(embedded_nul), "embedded-nul.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&nul_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(errdetail != NULL);
  free(errdetail);

  const char empty[] = "";
  ParseInput empty_input = {empty, 0, "empty.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&empty_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(absyn != NULL);
  ASSERT_TRUE(errdetail == NULL);
  as_delete(absyn);

  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(NULL, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_TRUE(errdetail == NULL);

  ParseInput null_data_input = {NULL, 0, "null-data.src"};
  CompilerDiagnostic diag = {0};
  SCANNER_STATE_t state = {0};
  rc = parse_source_compiler_diag(&null_data_input, &absyn, &errdetail,
                                  &diag, &state);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(1, state.line);
  ASSERT_EQ_INT(1, state.column);
  ASSERT_EQ_INT(1, state.span);
  ASSERT_TRUE(state.offending_token == NULL);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, diag.phase);
  compiler_diag_reset(&diag);

  rc = parse_source(&empty_input, NULL, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  rc = parse_source(&empty_input, &absyn, NULL);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);

  size_t literal_len = SIN_MAX_STRING_BYTES + 1;
  size_t source_len = literal_len + 3;
  char *large_literal = malloc(source_len + 1);
  ASSERT_NOT_NULL(large_literal);
  large_literal[0] = '"';
  memset(large_literal + 1, 'x', literal_len);
  large_literal[literal_len + 1] = '"';
  large_literal[literal_len + 2] = ';';
  large_literal[source_len] = '\0';
  ParseInput large_input = {large_literal, source_len, "large-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&large_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "String literal too long.") != NULL);
  free(errdetail);
  free(large_literal);
}
