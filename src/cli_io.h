#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
  CLI_IO_OK = 0,
  CLI_IO_INVALID_ARGUMENT,
  CLI_IO_OPEN_FAILED,
  CLI_IO_SEEK_FAILED,
  CLI_IO_TOO_LARGE,
  CLI_IO_ALLOC_FAILED,
  CLI_IO_READ_FAILED,
  CLI_IO_WRITE_FAILED,
  CLI_IO_CLOSE_FAILED
} CliIoStatusCode;

typedef struct {
  CliIoStatusCode code;
  int sys_error;
} CliIoStatus;

CliIoStatus cli_io_read_file_bytes(const char *path, uint8_t **out_data,
                                   size_t *out_len);
CliIoStatus cli_io_read_stdin_bytes(uint8_t **out_data, size_t *out_len);
CliIoStatus cli_io_read_source_text(const char *path, char **out_data,
                                    size_t *out_len);
CliIoStatus cli_io_write_bytes(const char *path, const uint8_t *data,
                               size_t len);
const char *cli_io_status_message(CliIoStatus status);
const char *cli_io_status_detail(CliIoStatus status);
