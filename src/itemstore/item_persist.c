// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "config.h"
#include "error.h"
#include "memory.h"
#include "log.h"
#include "item_internal.h"
#include "item_persist_internal.h"
#include "string_limits.h"

// The configuration object, defined in src/sin.c
extern CONFIG_t config;

static ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t
    load_constructor_failure_hook;
static ITEMSTORE_SYNC_HOOK_t sync_hook_override;

bool itemstore_default_sync_hook(FILE *file, const char *path) {
#ifdef _WIN32
  (void)file;
  (void)path;
  return true;
#else
  if (fsync(fileno(file)) != 0) {
    logerr("Failed to sync temporary itemstore %s: %s\n", path,
           strerror(errno));
    return false;
  }
  return true;
#endif
}

static bool itemstore_default_directory_sync_hook(const char *path) {
#ifdef _WIN32
  (void)path;
  return true;
#else
  char *dircopy = strdup(path);
  if (dircopy == NULL) {
    logerr("Failed to allocate directory path for itemstore %s: %s\n", path,
           strerror(errno));
    return false;
  }
  char *directory = dirname(dircopy);
  int directory_fd = open(directory, O_RDONLY);
  if (directory_fd < 0) {
    logerr("Failed to open itemstore directory %s for sync: %s\n", directory,
           strerror(errno));
    free(dircopy);
    return false;
  }

  bool success = true;
  if (fsync(directory_fd) != 0) {
    logerr("Failed to sync itemstore directory %s: %s\n", directory,
           strerror(errno));
    success = false;
  }
  if (close(directory_fd) != 0) {
    logerr("Failed to close itemstore directory %s after sync: %s\n",
           directory, strerror(errno));
    success = false;
  }
  free(dircopy);
  return success;
#endif
}

void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook) {
  sync_hook_override = hook;
}

void itemstore_set_load_constructor_failure_hook_for_tests(
    ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t hook) {
  load_constructor_failure_hook = hook;
}

ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t
itemstore_load_constructor_failure_hook(void) {
  return load_constructor_failure_hook;
}

static ITEMSTORE_DIRECTORY_SYNC_HOOK_t directory_sync_hook =
    itemstore_default_directory_sync_hook;

void itemstore_set_directory_sync_hook_for_tests(
    ITEMSTORE_DIRECTORY_SYNC_HOOK_t hook) {
  directory_sync_hook = hook != NULL
      ? hook
      : itemstore_default_directory_sync_hook;
}

static ITEMSTORE_PRE_PUBLISH_HOOK_t pre_publish_hook;

void itemstore_set_pre_publish_hook_for_tests(
    ITEMSTORE_PRE_PUBLISH_HOOK_t hook) {
  pre_publish_hook = hook;
}

bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability) {
  return durability != ITEMSTORE_DURABLE_FAST;
}

/* The on-disk contract is documented in docs/itemstore-format.md. */
#define ITEMSTORE_V1_MAGIC "SINITEM"
#define ITEMSTORE_V1_MAGIC_SIZE ((uint32_t)sizeof(ITEMSTORE_V1_MAGIC))
static bool write_bytes(FILE *file, const void *data, size_t length,
                        const char *context) {
  if (length == 0) return true;

  size_t written = fwrite(data, 1, length, file);
  if (written == length) return true;

  if (ferror(file)) {
    logerr("Failed to write itemstore %s: wrote %zu of %zu bytes: %s\n",
           context, written, length, strerror(errno));
  } else {
    logerr("Failed to write itemstore %s: wrote %zu of %zu bytes.\n",
           context, written, length);
  }
  return false;
}

static FILE *create_temp_itemstore(const char *filename, char **temp_path_out) {
  int temp_path_len = snprintf(NULL, 0, "%s.tmp.XXXXXX", filename);
  if (temp_path_len < 0) {
    logerr("Failed to build temporary itemstore path for %s.\n", filename);
    return NULL;
  }

  size_t temp_path_size = (size_t)temp_path_len + 1u;
  char *temp_path = malloc(temp_path_size);
  if (temp_path == NULL) {
    logerr("Failed to allocate temporary itemstore path for %s.\n", filename);
    return NULL;
  }
  (void)snprintf(temp_path, temp_path_size, "%s.tmp.XXXXXX", filename);

#ifdef _WIN32
  int file_descriptor = -1;
  for (unsigned attempt = 0; attempt < 100u; attempt++) {
    (void)snprintf(temp_path, temp_path_size, "%s.tmp.XXXXXX", filename);
    errno_t name_result = _mktemp_s(temp_path, temp_path_size);
    if (name_result != 0) {
      logerr("Failed to create temporary itemstore name beside %s: %s\n",
             filename, strerror(name_result));
      free(temp_path);
      return NULL;
    }
    file_descriptor = -1;
    errno_t open_result = _sopen_s(&file_descriptor, temp_path,
                                   _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                                   _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (open_result == 0) break;
    if (open_result != EEXIST) {
      logerr("Failed to create temporary itemstore %s: %s\n", temp_path,
             strerror(open_result));
      free(temp_path);
      return NULL;
    }
  }
  if (file_descriptor < 0) {
    logerr("Failed to create an unused temporary itemstore beside %s: %s\n",
           filename, strerror(EEXIST));
    free(temp_path);
    return NULL;
  }
  FILE *file = _fdopen(file_descriptor, "wb");
  if (file == NULL) {
    int saved_errno = errno;
    int close_result = _close(file_descriptor);
    if (close_result != 0) {
      logerr("Failed to close temporary itemstore descriptor %s: %s\n",
             temp_path, strerror(errno));
    }
    if (remove(temp_path) != 0 && errno != ENOENT) {
      logerr("Failed to remove temporary itemstore %s: %s\n", temp_path,
             strerror(errno));
    }
    logerr("Failed to open temporary itemstore %s as a stream: %s\n",
           temp_path, strerror(saved_errno));
    free(temp_path);
    return NULL;
  }
#else
  int file_descriptor = mkstemp(temp_path);
  if (file_descriptor < 0) {
    logerr("Failed to create temporary itemstore beside %s: %s\n", filename,
           strerror(errno));
    free(temp_path);
    return NULL;
  }
  FILE *file = fdopen(file_descriptor, "wb");
  if (file == NULL) {
    int saved_errno = errno;
    (void)close(file_descriptor);
    (void)remove(temp_path);
    logerr("Failed to open temporary itemstore %s as a stream: %s\n",
           temp_path, strerror(saved_errno));
    free(temp_path);
    return NULL;
  }
#endif

  *temp_path_out = temp_path;
  return file;
}

bool itemstore_read_bytes(FILE *file, void *data, size_t length,
                          const char *context) {
  if (length == 0) return true;

  size_t bytes_read = fread(data, 1, length, file);
  if (bytes_read == length) return true;

  if (ferror(file)) {
    logerr("Failed to read itemstore %s: read %zu of %zu bytes: %s\n",
           context, bytes_read, length, strerror(errno));
  } else if (feof(file)) {
    logerr("Failed to read itemstore %s: unexpected end of file after %zu "
           "of %zu bytes.\n", context, bytes_read, length);
  } else {
    logerr("Failed to read itemstore %s: read %zu of %zu bytes.\n",
           context, bytes_read, length);
  }
  return false;
}

static bool write_u8(FILE *file, uint8_t value, const char *context) {
  return write_bytes(file, &value, sizeof(value), context);
}

static bool write_u16_le(FILE *file, uint16_t value, const char *context) {
  uint8_t bytes[2] = {
    (uint8_t)(value & UINT16_C(0xff)),
    (uint8_t)((value >> 8) & UINT16_C(0xff))
  };
  return write_bytes(file, bytes, sizeof(bytes), context);
}

static bool write_u32_le(FILE *file, uint32_t value, const char *context) {
  uint8_t bytes[4];
  for (size_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = (uint8_t)((value >> (i * 8)) & UINT32_C(0xff));
  }
  return write_bytes(file, bytes, sizeof(bytes), context);
}

static bool write_u64_le(FILE *file, uint64_t value, const char *context) {
  uint8_t bytes[8];
  for (size_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = (uint8_t)((value >> (i * 8)) & UINT64_C(0xff));
  }
  return write_bytes(file, bytes, sizeof(bytes), context);
}

bool itemstore_read_u8(FILE *file, uint8_t *value, const char *context) {
  return itemstore_read_bytes(file, value, sizeof(*value), context);
}

bool itemstore_read_u16_le(FILE *file, uint16_t *value, const char *context) {
  uint8_t bytes[2];
  if (!itemstore_read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = (uint16_t)(((uint16_t)bytes[0]) | ((uint16_t)bytes[1] << 8));
  return true;
}

bool itemstore_read_u32_le(FILE *file, uint32_t *value, const char *context) {
  uint8_t bytes[4];
  if (!itemstore_read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = 0;
  for (size_t i = 0; i < sizeof(bytes); i++) {
    *value |= (uint32_t)bytes[i] << (i * 8);
  }
  return true;
}

bool itemstore_read_u64_le(FILE *file, uint64_t *value, const char *context) {
  uint8_t bytes[8];
  if (!itemstore_read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = 0;
  for (size_t i = 0; i < sizeof(bytes); i++) {
    *value |= (uint64_t)bytes[i] << (i * 8);
  }
  return true;
}

bool write_item(FILE *file, ITEM_t *item) {
  size_t depth = 0;
  for (ITEM_t *ancestor = item->parent; ancestor != NULL;
       ancestor = ancestor->parent) {
    depth++;
  }
  if (depth > ITEM_MAX_DEPTH) {
    logerr("Failed to write itemstore item '%s': depth %zu exceeds maximum "
           "%u.\n", item->name, depth, ITEM_MAX_DEPTH);
    return false;
  }

  size_t name_len = strnlen(item->name, ITEM_MAX_LAYER_NAME_LENGTH + 1u);
  if (name_len > ITEM_MAX_LAYER_NAME_LENGTH) {
    logerr("Failed to write itemstore item: name exceeds %u bytes.\n",
           ITEM_MAX_LAYER_NAME_LENGTH);
    return false;
  }
  if (item->parent != NULL && !is_valid_layer(item->name)) {
    logerr("Failed to write itemstore item: invalid layer name '%s'.\n",
           item->name);
    return false;
  }
  if (!write_u8(file, (uint8_t)name_len, "item name length")
      || !write_bytes(file, item->name, name_len, "item name")) {
    return false;
  }

  ITEMSTORE_V1_ITEM_TAG_t item_tag;
  switch (item->type) {
    case ITEM_value: item_tag = ITEMSTORE_V1_ITEM_TAG_VALUE; break;
    case ITEM_code: item_tag = ITEMSTORE_V1_ITEM_TAG_CODE; break;
    default:
      logerr("Failed to write itemstore item '%s': unsupported item type %d.\n",
             item->name, item->type);
      return false;
  }
  if (!write_u8(file, item_tag, "item type tag")) return false;

  if (item->type == ITEM_value) {
    ITEMSTORE_V1_VALUE_TAG_t value_tag;
    switch (item->value.type) {
      case VALUE_nil: value_tag = ITEMSTORE_V1_VALUE_TAG_NIL; break;
      case VALUE_int: value_tag = ITEMSTORE_V1_VALUE_TAG_INT; break;
      case VALUE_float: value_tag = ITEMSTORE_V1_VALUE_TAG_FLOAT; break;
      case VALUE_str: value_tag = ITEMSTORE_V1_VALUE_TAG_STRING; break;
      case VALUE_bool: value_tag = ITEMSTORE_V1_VALUE_TAG_BOOL; break;
      case VALUE_itemref:
      case VALUE_list:
      default:
        logerr("Failed to write itemstore item '%s': unsupported value type "
               "%d.\n", item->name, item->value.type);
        return false;
    }
    if (!write_u8(file, value_tag, "value type tag")) return false;

    switch (item->value.type) {
      case VALUE_nil:
        break;
      case VALUE_int:
      {
        uint64_t payload;
        memcpy(&payload, &item->value.i, sizeof(payload));
        if (!write_u64_le(file, payload,
                          "integer payload")) return false;
        break;
      }
      case VALUE_float:
        if (!write_u64_le(file, item->value.f_bits,
                          "float payload")) return false;
        break;
      case VALUE_str:
      {
        size_t length = strlen(item->value.s);
        if (length > SIN_MAX_STRING_BYTES) {
          logerr("Failed to write itemstore string payload for '%s': length "
                 "%zu exceeds maximum %zu.\n", item->name, length,
                 SIN_MAX_STRING_BYTES);
          return false;
        }
        if (!write_u32_le(file, (uint32_t)length, "string length")
            || !write_bytes(file, item->value.s, length, "string payload")) {
          return false;
        }
        break;
      }
      case VALUE_bool:
        if (!write_u8(file, item->value.i ? 1u : 0u,
                      "boolean payload")) return false;
        break;
      case VALUE_itemref:
      case VALUE_list:
      default:
        return false;
    }
  } else if (item->type == ITEM_code) {
    if (item->bytecode_len > ITEMSTORE_MAX_BYTECODE_LEN) {
      logerr("Failed to write itemstore bytecode for '%s': length %u exceeds "
             "maximum %u.\n", item->name, item->bytecode_len,
             ITEMSTORE_MAX_BYTECODE_LEN);
      return false;
    }
    if (item->bytecode_len > 0 && item->bytecode == NULL) {
      logerr("Failed to write itemstore bytecode for '%s': length is %u but "
             "bytecode is NULL.\n", item->name, item->bytecode_len);
      return false;
    }
    if (!write_u32_le(file, item->bytecode_len, "bytecode length")) return false;
    if (!write_bytes(file, item->bytecode, item->bytecode_len,
                     "bytecode payload")) return false;
  }

  size_t numchildren = item_children_count(item->children);
  if (numchildren > ITEMSTORE_MAX_CHILDREN_PER_ITEM) {
    logerr("Failed to write itemstore item '%s': child count %zu exceeds "
           "maximum %u.\n", item->name, numchildren,
           ITEMSTORE_MAX_CHILDREN_PER_ITEM);
    return false;
  }
  if (!write_u32_le(file, (uint32_t)numchildren, "child count")) return false;

  for (size_t i = 0; i < numchildren; i++) {
    if (!write_item(file, item_children_at(item->children, i))) return false;
  }
  return true;
}

// Shared persistence publish core.  Writes a temp file, then publishes via
// rename (replace mode) or link+unlink (no-replace mode).  Returns a result
// enum that distinguishes success, collision, and failure.  Publication is
// tracked separately so failures after publication are diagnosed accurately.
typedef enum {
  ITEMSTORE_PUBLISH_REPLACE,
  ITEMSTORE_PUBLISH_NO_REPLACE
} ITEMSTORE_PUBLISH_MODE_e;

static bool remove_temp_itemstore(const char *temp_path) {
  if (remove(temp_path) == 0 || errno == ENOENT) return true;
  logerr("Failed to remove temporary itemstore %s: %s\n", temp_path,
         strerror(errno));
  if (remove(temp_path) == 0 || errno == ENOENT) return true;
  logerr("Retry removal of temporary itemstore %s also failed: %s\n",
         temp_path, strerror(errno));
  return false;
}

static int link_itemstore_no_replace(const char *temp_path,
                                     const char *filename) {
#ifdef _WIN32
  return _link(temp_path, filename);
#else
  return link(temp_path, filename);
#endif
}

static ITEMSTORE_SAVE_RESULT_e itemstore_save_core(
    const char *filename, ITEM_t *root,
    ITEMSTORE_DURABILITY_e durability,
    ITEMSTORE_PUBLISH_MODE_e mode) {
  FILE *file = NULL;
  char *temp_path = NULL;
  ITEMSTORE_SAVE_RESULT_e result = ITEMSTORE_SAVE_FAILURE;
  bool published = false;
  bool temp_needs_cleanup = false;

  file = create_temp_itemstore(filename, &temp_path);
  if (file == NULL) return ITEMSTORE_SAVE_FAILURE;
  temp_needs_cleanup = true;

  if (!write_bytes(file, ITEMSTORE_V1_MAGIC, ITEMSTORE_V1_MAGIC_SIZE,
                   "file-header magic")
      || !write_u16_le(file, ITEMSTORE_V1_FORMAT_VERSION,
                       "file-header version")
      || !write_item(file, root)) {
    goto cleanup;
  }

  if (fflush(file) != 0) {
    logerr("Failed to flush temporary itemstore %s: %s\n", temp_path,
           strerror(errno));
    goto cleanup;
  }

  if (itemstore_durability_requires_sync(durability)
      && !(sync_hook_override != NULL
               ? sync_hook_override
               : (root != NULL && root->store != NULL
                      && root->store->context.sync_hook != NULL
                  ? root->store->context.sync_hook
                  : itemstore_default_sync_hook))(file, temp_path)) {
    logerr("Failed to sync temporary itemstore %s.\n", temp_path);
    goto cleanup;
  }

  if (fclose(file) != 0) {
    file = NULL;
    logerr("Failed to close temporary itemstore %s: %s\n", temp_path,
           strerror(errno));
    goto cleanup;
  }
  file = NULL;

  if (mode == ITEMSTORE_PUBLISH_REPLACE) {
    if (rename(temp_path, filename) != 0) {
      logerr("Failed to replace itemstore %s with %s: %s\n", filename,
             temp_path, strerror(errno));
      goto cleanup;
    }
    published = true;
    temp_needs_cleanup = false;
  } else {
    if (pre_publish_hook) pre_publish_hook(filename);
    if (link_itemstore_no_replace(temp_path, filename) != 0) {
      if (errno == EEXIST) {
        result = ITEMSTORE_SAVE_TARGET_EXISTS;
      } else {
        logerr("Failed to link temporary itemstore %s to %s: %s\n",
               temp_path, filename, strerror(errno));
      }
      goto cleanup;
    }
    published = true;
    temp_needs_cleanup = false;
    if (!remove_temp_itemstore(temp_path)) {
      goto cleanup;
    }
  }

  if (itemstore_durability_requires_sync(durability)
      && !directory_sync_hook(filename)) {
    logerr("Failed to sync the containing directory after publishing itemstore "
           "%s.\n", filename);
    goto cleanup;
  }

  result = ITEMSTORE_SAVE_SUCCESS;

cleanup:
  if (file != NULL && fclose(file) != 0) {
    logerr("Failed to close temporary itemstore %s during cleanup: %s\n",
           temp_path, strerror(errno));
  }
  if (temp_needs_cleanup && temp_path && !remove_temp_itemstore(temp_path)
      && result == ITEMSTORE_SAVE_TARGET_EXISTS) {
    result = ITEMSTORE_SAVE_FAILURE;
  }

  switch (result) {
    case ITEMSTORE_SAVE_FAILURE:
      if (published) {
        logerr("Failed to save itemstore '%s'; publication already happened, "
               "so the destination may contain the new data.\n", filename);
      } else {
        logerr("Failed to save itemstore '%s'; existing data was not replaced.\n",
               filename);
      }
      break;
    case ITEMSTORE_SAVE_TARGET_EXISTS:
      /* No message: the caller will handle the collision retry. */
      break;
    case ITEMSTORE_SAVE_SUCCESS:
      break;
  }

  free(temp_path);
  return result;
}

bool save_itemstore_with_options(const char *filename, ITEM_t *root,
                                 ITEMSTORE_DURABILITY_e durability) {
  ITEMSTORE_SAVE_RESULT_e r = itemstore_save_core(filename, root, durability,
                                                   ITEMSTORE_PUBLISH_REPLACE);
  return r == ITEMSTORE_SAVE_SUCCESS;
}

ITEMSTORE_SAVE_RESULT_e save_itemstore_no_replace(
    const char *filename, ITEM_t *root, ITEMSTORE_DURABILITY_e durability) {
  return itemstore_save_core(filename, root, durability,
                             ITEMSTORE_PUBLISH_NO_REPLACE);
}

ITEMSTORE_READ_CTX_t itemstore_read_context(const char *filename,
                                            size_t depth) {
  ITEMSTORE_READ_CTX_t ctx = {
    .depth = depth,
    .max_depth = ITEM_MAX_DEPTH,
    .max_children_per_item = ITEMSTORE_MAX_CHILDREN_PER_ITEM,
    .max_string_len = (uint32_t)SIN_MAX_STRING_BYTES,
    .max_bytecode_len = ITEMSTORE_MAX_BYTECODE_LEN,
    .filename = filename,
    .strict_validation = false
  };
  return ctx;
}

ITEM_t *itemstore_read_record_for_version(
    uint16_t version, FILE *file, ITEM_t *parent, ITEMSTORE_READ_CTX_t *ctx) {
  if (version == ITEMSTORE_V1_FORMAT_VERSION) {
    return itemstore_read_v1_record(file, parent, ctx);
  }
  logerr("Unsupported itemstore version in '%s': found %u, supported "
         "version is %u.\n", ctx->filename, version,
         ITEMSTORE_V1_FORMAT_VERSION);
  return NULL;
}

ITEM_t *read_item(FILE *file, ITEM_t *parent) {
  size_t depth = 0;
  for (ITEM_t *ancestor = parent; ancestor != NULL;
       ancestor = ancestor->parent) {
    depth++;
  }
  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context("<stream>", depth);
  return itemstore_read_v1_record(file, parent, &ctx);
}

static bool read_itemstore_header(FILE *file, const char *filename,
                                  uint16_t *version_out) {
  uint8_t magic[ITEMSTORE_V1_MAGIC_SIZE];
  uint16_t version;

  if (!itemstore_read_bytes(file, magic, sizeof(magic), "file-header magic")) {
    logerr("Corrupt itemstore '%s': truncated file header magic.\n", filename);
    return false;
  }
  if (memcmp(magic, ITEMSTORE_V1_MAGIC, sizeof(magic)) != 0) {
    logerr("Corrupt itemstore '%s': invalid itemstore magic.\n", filename);
    return false;
  }
  if (!itemstore_read_u16_le(file, &version, "file-header version")) {
    logerr("Corrupt itemstore '%s': truncated file header version.\n",
           filename);
    return false;
  }
  *version_out = version;
  return true;
}

ITEM_t *load_itemstore_with_options(const char *filename,
                                    bool strict_validation) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    logerr("Failed to open itemstore '%s' for reading: %s\n",
           filename, strerror(errno));
    return NULL;
  }

  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context(filename, 0);
  ctx.strict_validation = strict_validation;
  ITEM_t *root = NULL;
  uint16_t version = 0;
  if (read_itemstore_header(file, filename, &version)) {
    root = itemstore_read_record_for_version(version, file, NULL, &ctx);
  }
  if (root != NULL) {
    int trailing = fgetc(file);
    if (trailing != EOF) {
      logerr("Corrupt itemstore '%s': trailing data after the root record.\n",
             filename);
      destroy_item(root);
      root = NULL;
    } else if (ferror(file)) {
      logerr("Failed to verify the end of itemstore '%s': %s\n",
             filename, strerror(errno));
      destroy_item(root);
      root = NULL;
    }
  }

  if (fclose(file) != 0) {
    logerr("Failed to close itemstore '%s' after reading: %s\n",
           filename, strerror(errno));
    if (root) destroy_item(root);
    return NULL;
  }
  if (!root) {
    logerr("Failed to load itemstore '%s': invalid or truncated data.\n",
           filename);
  }
  return root;
}

ITEM_t *load_itemstore(const char *filename) {
  return load_itemstore_with_options(filename, config.strict_validation);
}

bool save_itemstore(const char *filename, ITEM_t *root) {
  return save_itemstore_with_options(filename, root, config.itemstore_durability);
}

void dump_item(ITEM_t *item, char *item_name, bool isroot) {
  // Recursive function to construct and print the fully-qualified itemstore
  // from a given node. If passing the root of the itemstore,
  // item_name == NULL and isroot == true
  // Base case: if the item is NULL, return
  if (item == NULL) return;
  // Buffer to hold the full name of the current item
  char currentpath[265]; // 8 layers + 7 dots + 1 \0
  if (isroot) {
    // For the root item, we initialize the path as empty
    currentpath[0] = '\0';
  } else {
    // If a path is provided, use it
    // otherwise start with the current item's name
    if (item_name && item_name[0] != '\0') {
      snprintf(currentpath, sizeof(currentpath), "%s.%s", item_name,
                                                             item->name);
    } else {
      snprintf(currentpath, sizeof(currentpath), "%s", item->name);
    }
  }
  // Only print if this is not the root item
  if (!isroot) {
    if (item->value.type == VALUE_int) {
      logverbose("Item: %s, Value: %llu\n", currentpath,
                 (unsigned long long)item->value.i);
    } else if (item->value.type == VALUE_str) {
      logverbose("Item: %s, Value: '%s'\n", currentpath, item->value.s);
    } else {
      logverbose("Item: %s, Value: (unknown)\n", currentpath);
    }
  }
  for (size_t i = 0; i < item_children_count(item->children); i++) {
    dump_item(item_children_at(item->children, i), currentpath, false);
  }
}
