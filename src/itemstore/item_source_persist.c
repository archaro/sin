// Source-sidecar persistence for items.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#include "log.h"
#include "util.h"
#include "item_internal.h"
#include "string_limits.h"

static ITEMSTORE_SOURCE_WRITE_HOOK_t source_write_hook = fputs;
static ITEMSTORE_SOURCE_CLOSE_HOOK_t source_close_hook = fclose;

void itemstore_set_source_io_hooks_for_tests(
    ITEMSTORE_SOURCE_WRITE_HOOK_t write_hook,
    ITEMSTORE_SOURCE_CLOSE_HOOK_t close_hook) {
  source_write_hook = write_hook != NULL ? write_hook : fputs;
  source_close_hook = close_hook != NULL ? close_hook : fclose;
}

bool save_itemsource_in_srcroot(ITEM_t *item, char *source, const char *srcroot) {
  // Saves the item source into srcroot.
  // If the source cannot be saved for whatever reason, this is
  // reported in the error log.  The function returns true if the
  // source was saved, otherwise false.

  char *filename = get_itemfilename_in_srcroot(item, srcroot);
  if (filename == NULL) {
    logerr("Failed to allocate source filename.\n");
    return false;
  }
  // There is a much better way to do this, but I don't care right now.
  char *dircopy = strdup(filename);
  if (dircopy == NULL) {
    logerr("Failed to allocate source directory path for %s.\n", filename);
    free(filename);
    return false;
  }
  char *dir = dirname(dircopy);
  bool res = make_path(dir);
  free(dircopy);
  if (!res) {
    free(filename);
    return false;
  }
  // When we arrive here, we know that the path exists.
  FILE *out = fopen(filename, "w");
  if (!out) {
    logerr("Failed to open file %s: %s\n", filename, strerror(errno));
    free(filename);
    return false;
  }
  bool success = true;
  if (source_write_hook(source, out) == EOF) {
    logerr("Failed to write text to file %s\n", filename);
    success = false;
  }
  if (source_close_hook(out) != 0) {
    logerr("Failed to close file %s\n", filename);
    success = false;
  }
  free(filename);
  return success;
}

bool save_itemsource(ITEM_t *item, char *source) {
  return save_itemsource_in_srcroot(item, source, NULL);
}

char *read_itemsource_in_srcroot(ITEM_t *item, const char *srcroot,
                                 char *detail, size_t detail_size) {
  char *filename = NULL;
  FILE *file = NULL;
  char *source = NULL;

  if (detail && detail_size > 0) detail[0] = '\0';
  if (!item) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size, "source item is unavailable");
    }
    return NULL;
  }
  if (!srcroot || srcroot[0] == '\0') {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size, "source root is unconfigured");
    }
    return NULL;
  }

  filename = get_itemfilename_in_srcroot(item, srcroot);
  if (!filename) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to allocate source file path");
    }
    return NULL;
  }

  file = fopen(filename, "rb");
  if (!file) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to open source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to seek source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }
  long end = ftell(file);
  if (end < 0) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to measure source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }
  if ((uintmax_t)end > (uintmax_t)SIN_MAX_STRING_BYTES) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "source file '%s' exceeds the %zu-byte string limit",
                     filename, SIN_MAX_STRING_BYTES);
    }
    goto fail;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to rewind source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }

  size_t source_len = (size_t)end;
  source = malloc(source_len + 1u);
  if (!source) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to allocate source result for '%s'", filename);
    }
    goto fail;
  }
  if (source_len > 0 && fread(source, 1, source_len, file) != source_len) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to read source file '%s': %s", filename,
                     ferror(file) ? strerror(errno) : "unexpected end of file");
    }
    goto fail;
  }
  int extra = fgetc(file);
  if (extra != EOF) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "source file '%s' changed while being read", filename);
    }
    goto fail;
  }
  if (ferror(file)) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to finish reading source file '%s': %s",
                     filename, strerror(errno));
    }
    goto fail;
  }
  source[source_len] = '\0';
  if (memchr(source, '\0', source_len) != NULL) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "source file '%s' contains an embedded NUL", filename);
    }
    goto fail;
  }
  if (fclose(file) != 0) {
    file = NULL;
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to close source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }
  file = NULL;
  free(filename);
  return source;

fail:
  if (file) (void)fclose(file);
  free(source);
  free(filename);
  return NULL;
}
