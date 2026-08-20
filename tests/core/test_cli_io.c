#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_io.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_cli_io_helpers(void) {
  char path[4096], copy_path[4096], missing_path[4096];
  snprintf(path, sizeof path, "%s/cli-io.tmp.bin", test_temp_root());
  snprintf(copy_path, sizeof copy_path, "%s/cli-io-copy.tmp.bin", test_temp_root());
  snprintf(missing_path, sizeof missing_path, "%s/cli-io-missing.tmp.bin", test_temp_root());
  const uint8_t bytes[] = {'a', 'b', '\0', 'c', '\n'};
  CliIoStatus status = cli_io_write_bytes(path, bytes, sizeof(bytes));
  ASSERT_EQ_INT(CLI_IO_OK, status.code);

  uint8_t *read_bytes = NULL;
  size_t read_len = 0;
  status = cli_io_read_file_bytes(path, &read_bytes, &read_len);
  ASSERT_EQ_INT(CLI_IO_OK, status.code);
  ASSERT_EQ_INT((int)sizeof(bytes), (int)read_len);
  ASSERT_TRUE(memcmp(bytes, read_bytes, sizeof(bytes)) == 0);

  char *text = NULL;
  size_t text_len = 0;
  status = cli_io_read_source_text(path, &text, &text_len);
  ASSERT_EQ_INT(CLI_IO_OK, status.code);
  ASSERT_EQ_INT((int)sizeof(bytes), (int)text_len);
  ASSERT_TRUE(memcmp(bytes, text, sizeof(bytes)) == 0);
  ASSERT_EQ_INT('\0', text[text_len]);

  status = cli_io_write_bytes(copy_path, read_bytes, read_len);
  ASSERT_EQ_INT(CLI_IO_OK, status.code);
  uint8_t *copy_bytes = NULL;
  size_t copy_len = 0;
  status = cli_io_read_file_bytes(copy_path, &copy_bytes, &copy_len);
  ASSERT_EQ_INT(CLI_IO_OK, status.code);
  ASSERT_EQ_INT((int)read_len, (int)copy_len);
  ASSERT_TRUE(memcmp(read_bytes, copy_bytes, read_len) == 0);

  uint8_t *missing = NULL;
  size_t missing_len = 123;
  status = cli_io_read_file_bytes(missing_path,
                                  &missing, &missing_len);
  ASSERT_EQ_INT(CLI_IO_OPEN_FAILED, status.code);
  ASSERT_TRUE(status.sys_error != 0);
  ASSERT_TRUE(strcmp(cli_io_status_detail(status),
                     cli_io_status_message(status)) != 0);
  ASSERT_TRUE(missing == NULL);
  ASSERT_EQ_INT(0, (int)missing_len);

  free(read_bytes);
  free(text);
  free(copy_bytes);
  remove(path);
  remove(copy_path);
}
