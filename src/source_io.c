#include "source_io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "memory.h"

int load_file_buffer(const char *path, char **out_data, size_t *out_len) {
  FILE *in = NULL;
  long file_len = 0;
  char *buf = NULL;
  size_t bytes_read = 0;

  if (!path || !out_data || !out_len) return -1;

  *out_data = NULL;
  *out_len = 0;

  in = fopen(path, "r");
  if (!in) return -1;
  if (fseek(in, 0, SEEK_END) != 0) goto fail;
  file_len = ftell(in);
  if (file_len < 0 || file_len > INT_MAX) goto fail;
  if (fseek(in, 0, SEEK_SET) != 0) goto fail;

  buf = GROW_ARRAY(char, NULL, 0, (int)file_len);
  bytes_read = fread(buf, sizeof(char), (size_t)file_len, in);
  if (bytes_read != (size_t)file_len) goto fail;

  fclose(in);
  *out_data = buf;
  *out_len = (size_t)file_len;
  return 0;

fail:
  if (in) fclose(in);
  if (buf) FREE_ARRAY(char, buf, (int)file_len);
  return -1;
}
