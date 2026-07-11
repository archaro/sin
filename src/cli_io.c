#include "cli_io.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CliIoStatus status_make(CliIoStatusCode code, int sys_error) {
  CliIoStatus status = {code, sys_error};
  return status;
}

const char *cli_io_status_message(CliIoStatus status) {
  switch (status.code) {
    case CLI_IO_OK: return "success";
    case CLI_IO_INVALID_ARGUMENT: return "invalid argument";
    case CLI_IO_OPEN_FAILED: return "unable to open file";
    case CLI_IO_SEEK_FAILED: return "unable to seek file";
    case CLI_IO_TOO_LARGE: return "input is too large";
    case CLI_IO_ALLOC_FAILED: return "unable to allocate buffer";
    case CLI_IO_READ_FAILED: return "unable to read complete input";
    case CLI_IO_WRITE_FAILED: return "unable to write complete output";
    case CLI_IO_CLOSE_FAILED: return "unable to close output file";
  }
  return "unknown IO error";
}

const char *cli_io_status_detail(CliIoStatus status) {
  if (status.sys_error != 0) return strerror(status.sys_error);
  return cli_io_status_message(status);
}

static CliIoStatus read_stream(FILE *stream, uint8_t **out_data,
                               size_t *out_len) {
  size_t cap = 4096;
  size_t len = 0;
  uint8_t *buf = malloc(cap);
  if (!buf) return status_make(CLI_IO_ALLOC_FAILED, 0);

  for (;;) {
    if (len == cap) {
      if (cap > SIZE_MAX / 2) {
        free(buf);
        return status_make(CLI_IO_TOO_LARGE, 0);
      }
      size_t next_cap = cap * 2;
      uint8_t *next = realloc(buf, next_cap);
      if (!next) {
        free(buf);
        return status_make(CLI_IO_ALLOC_FAILED, 0);
      }
      buf = next;
      cap = next_cap;
    }

    size_t bytes_read = fread(buf + len, 1, cap - len, stream);
    len += bytes_read;
    if (bytes_read == 0) {
      if (ferror(stream)) {
        free(buf);
        return status_make(CLI_IO_READ_FAILED, errno);
      }
      break;
    }
  }

  *out_data = buf;
  *out_len = len;
  return status_make(CLI_IO_OK, 0);
}

CliIoStatus cli_io_read_stdin_bytes(uint8_t **out_data, size_t *out_len) {
  if (!out_data || !out_len) {
    return status_make(CLI_IO_INVALID_ARGUMENT, 0);
  }
  *out_data = NULL;
  *out_len = 0;
  return read_stream(stdin, out_data, out_len);
}

CliIoStatus cli_io_read_file_bytes(const char *path, uint8_t **out_data,
                                   size_t *out_len) {
  if (!path || !out_data || !out_len) {
    return status_make(CLI_IO_INVALID_ARGUMENT, 0);
  }
  *out_data = NULL;
  *out_len = 0;

  FILE *in = fopen(path, "rb");
  if (!in) return status_make(CLI_IO_OPEN_FAILED, errno);

  if (fseek(in, 0, SEEK_END) != 0) {
    int saved_errno = errno;
    fclose(in);
    return status_make(CLI_IO_SEEK_FAILED, saved_errno);
  }
  long file_len = ftell(in);
  if (file_len < 0 || (uint64_t)file_len > SIZE_MAX) {
    fclose(in);
    return status_make(CLI_IO_TOO_LARGE, 0);
  }
  if (fseek(in, 0, SEEK_SET) != 0) {
    int saved_errno = errno;
    fclose(in);
    return status_make(CLI_IO_SEEK_FAILED, saved_errno);
  }

  size_t len = (size_t)file_len;
  uint8_t *buf = malloc(len ? len : 1);
  if (!buf) {
    fclose(in);
    return status_make(CLI_IO_ALLOC_FAILED, 0);
  }
  if (len > 0 && fread(buf, 1, len, in) != len) {
    int saved_errno = errno;
    free(buf);
    fclose(in);
    return status_make(CLI_IO_READ_FAILED, saved_errno);
  }
  if (fclose(in) != 0) {
    free(buf);
    return status_make(CLI_IO_READ_FAILED, errno);
  }

  *out_data = buf;
  *out_len = len;
  return status_make(CLI_IO_OK, 0);
}

CliIoStatus cli_io_read_source_text(const char *path, char **out_data,
                                    size_t *out_len) {
  if (!path || !out_data || !out_len) {
    return status_make(CLI_IO_INVALID_ARGUMENT, 0);
  }
  *out_data = NULL;
  *out_len = 0;

  uint8_t *bytes = NULL;
  size_t len = 0;
  CliIoStatus status = strcmp(path, "-") == 0
                           ? cli_io_read_stdin_bytes(&bytes, &len)
                           : cli_io_read_file_bytes(path, &bytes, &len);
  if (status.code != CLI_IO_OK) return status;

  char *text = realloc(bytes, len + 1);
  if (!text) {
    free(bytes);
    return status_make(CLI_IO_ALLOC_FAILED, 0);
  }
  text[len] = '\0';
  *out_data = text;
  *out_len = len;
  return status_make(CLI_IO_OK, 0);
}

CliIoStatus cli_io_write_bytes(const char *path, const uint8_t *data,
                               size_t len) {
  if (!path || (!data && len > 0)) {
    return status_make(CLI_IO_INVALID_ARGUMENT, 0);
  }
  FILE *out = NULL;
  int to_stdout = strcmp(path, "-") == 0;
  out = to_stdout ? stdout : fopen(path, "wb");
  if (!out) return status_make(CLI_IO_OPEN_FAILED, errno);

  if (len > 0 && fwrite(data, 1, len, out) != len) {
    int saved_errno = errno;
    if (!to_stdout) fclose(out);
    return status_make(CLI_IO_WRITE_FAILED, saved_errno);
  }
  if (to_stdout) {
    if (fflush(out) != 0) {
      return status_make(CLI_IO_WRITE_FAILED, errno);
    }
  } else if (fclose(out) != 0) {
    return status_make(CLI_IO_CLOSE_FAILED, errno);
  }
  return status_make(CLI_IO_OK, 0);
}
