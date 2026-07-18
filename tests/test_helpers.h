#pragma once

#include <stddef.h>
#include <stdint.h>

#include "compiler/absyn.h"
#include "compiler/emitbc.h"
#include "compiler/ir.h"

AS_NODE *t_int(int64_t value);
AS_NODE *t_local(const char *name);
AS_NODE *t_node(ENUM_NODE nodetype, void *lhs, void *rhs);
AS_NODE *t_stmtlist_with_one(AS_NODE *stmt);

IR_Unit *t_new_unit(void);
void t_emit(IR_Unit *unit, IR_Inst inst);
void t_bind(IR_Unit *unit, int32_t label_id);

int8_t t_emit_bytecode(IR_Unit *unit, uint8_t local_count, uint8_t param_count,
                       OUTPUT_t *out, char **errdetail);

uint8_t hex_nibble(char c);
uint8_t *load_hex_fixture(const char *path, size_t *out_len);

void assert_bytes_equal_with_diag(const uint8_t *expected, size_t expected_len,
                                  const uint8_t *actual, size_t actual_len,
                                  const char *context);
void assert_file_bytes_equal(const char *expected_path, const char *actual_path,
                             const char *context);
void compile_source_and_assert_hex(const char *source, const char *fixture_path);

typedef struct {
  /* Text fields remain NUL-terminated; lengths also support binary output. */
  char *stdout_text;
  char *stderr_text;
  int exit_code;
  int timed_out;
  size_t stdout_length;
  size_t stderr_length;
} TestProcessResult;

char *test_read_text_file(const char *path);
char *test_normalize_text(char *text);
char *test_extract_fixture_block(const char *fixture, const char *header);
int test_contains_all_lines(const char *expected_lines, const char *actual,
                            int *missing_line);
int test_make_temp_path(const char *prefix, char *path, size_t path_size);
int test_run_argv_capture(char *const argv[], unsigned timeout_ms,
                          TestProcessResult *result);
int test_run_argv_capture_with_stdin(char *const argv[], const void *stdin_data,
                                     size_t stdin_length, unsigned timeout_ms,
                                     TestProcessResult *result);
void test_process_result_free(TestProcessResult *result);
