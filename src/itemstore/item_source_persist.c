// Source-sidecar persistence for items.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "log.h"
#include "config.h"
#include "util.h"
#include "item_internal.h"
#include "string_limits.h"

static ITEMSTORE_SOURCE_WRITE_HOOK_t source_write_hook = fputs;
static ITEMSTORE_SOURCE_CLOSE_HOOK_t source_close_hook = fclose;

extern CONFIG_t config;

static int open_source_directory(const char *itemname, const char *srcroot,
                                 bool create) {
  int current_fd = open(srcroot, O_RDONLY | O_DIRECTORY);
  if (current_fd < 0) return -1;

  char path[MAX_ITEM_NAME];
  int written = snprintf(path, sizeof path, "%s", itemname);
  if (written < 0 || (size_t)written >= sizeof path) {
    (void)close(current_fd);
    errno = ENAMETOOLONG;
    return -1;
  }

  char *layer = path;
  while (*layer != '\0') {
    char *dot = strchr(layer, '.');
    if (dot) *dot = '\0';

    if (create && mkdirat(current_fd, layer, S_IRWXU) != 0
        && errno != EEXIST) {
      int saved_errno = errno;
      (void)close(current_fd);
      errno = saved_errno;
      return -1;
    }

    int next_fd = openat(current_fd, layer,
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (next_fd < 0) {
      int saved_errno = errno;
      (void)close(current_fd);
      errno = saved_errno;
      return -1;
    }
    (void)close(current_fd);
    current_fd = next_fd;
    if (!dot) break;
    layer = dot + 1;
  }
  return current_fd;
}

static bool create_source_root(const char *srcroot) {
  char *rootcopy = strdup(srcroot);
  if (!rootcopy) {
    logerr("Failed to allocate source root path.\n");
    return false;
  }
  bool result = make_path(rootcopy);
  free(rootcopy);
  return result;
}

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

  if (!item || item->type != ITEM_code || !source || !srcroot ||
      srcroot[0] == '\0') return false;
  char *filename = get_itemfilename_in_srcroot(item, srcroot);
  if (filename == NULL) {
    logerr("Failed to construct source filename.\n");
    return false;
  }
  char itemname[MAX_ITEM_NAME];
  get_itemname(item, itemname);
  if (!create_source_root(srcroot)) {
    free(filename);
    return false;
  }

  int dir_fd = open_source_directory(itemname, srcroot, true);
  if (dir_fd < 0) {
    logerr("Failed to open source directory for %s: %s\n", filename,
           strerror(errno));
    free(filename);
    return false;
  }

  int out_fd = openat(dir_fd, "source.sin",
                      O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
  int open_errno = errno;
  (void)close(dir_fd);
  if (out_fd < 0) {
    errno = open_errno;
    logerr("Failed to open file %s: %s\n", filename, strerror(errno));
    free(filename);
    return false;
  }
  FILE *out = fdopen(out_fd, "w");
  if (!out) {
    logerr("Failed to associate stream with file %s: %s\n", filename,
           strerror(errno));
    (void)close(out_fd);
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
  return save_itemsource_in_srcroot(item, source, config.srcroot);
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
  if (item->type != ITEM_code) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size, "source item is not a code item");
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
                     "unable to construct source file path");
    }
    return NULL;
  }

  char itemname[MAX_ITEM_NAME];
  get_itemname(item, itemname);
  int dir_fd = open_source_directory(itemname, srcroot, false);
  if (dir_fd < 0) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to open source directory for '%s': %s",
                     filename, strerror(errno));
    }
    goto fail;
  }

  int in_fd = openat(dir_fd, "source.sin", O_RDONLY | O_NOFOLLOW);
  int open_errno = errno;
  (void)close(dir_fd);
  if (in_fd < 0) {
    errno = open_errno;
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to open source file '%s': %s", filename,
                     strerror(errno));
    }
    goto fail;
  }
  file = fdopen(in_fd, "rb");
  if (!file) {
    if (detail && detail_size > 0) {
      (void)snprintf(detail, detail_size,
                     "unable to associate stream with source file '%s': %s",
                     filename, strerror(errno));
    }
    (void)close(in_fd);
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
