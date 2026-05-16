#ifndef SOURCE_IO_H
#define SOURCE_IO_H

#include <stddef.h>

int load_file_buffer(const char *path, char **out_data, size_t *out_len);

#endif
