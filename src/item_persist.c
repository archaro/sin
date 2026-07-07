// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "config.h"
#include "error.h"
#include "util.h"
#include "memory.h"
#include "log.h"
#include "item_internal.h"
#include "bytecode_verify.h"

// The configuration object, defined in sin.c
extern CONFIG_t config;

static long current_process_id(void) {
#ifdef _WIN32
  return (long)_getpid();
#else
  return (long)getpid();
#endif
}

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

void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook) {
  itemstore_default_context()->sync_hook = hook != NULL
      ? hook
      : itemstore_default_sync_hook;
}

bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability) {
  return durability != ITEMSTORE_DURABLE_FAST;
}

bool save_itemsource(ITEM_t *item, char *source) {
  // Saves the item source into srcroot.
  // If the source cannot be saved for whatever reason, this is
  // reported in the error log.  The function returns true if the
  // source was saved, otherwise false.

  char *filename = get_itemfilename(item);
  // There is a much better way to do this, but I don't care right now.
  char *dircopy = strdup(filename);
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
  if (fputs(source, out) == EOF) {
    logerr("Failed to write text to file %s\n", filename);
  }
  if (fclose(out) != 0) {
    logerr("Failed to close file %s\n", filename);
  }
  free(filename);
  return true;
}

/* The on-disk contract is documented in docs/itemstore-format.md. */
#define ITEMSTORE_V1_MAGIC "SINITEM"
#define ITEMSTORE_V1_MAGIC_SIZE ((uint32_t)sizeof(ITEMSTORE_V1_MAGIC))
#define ITEMSTORE_V1_FORMAT_VERSION ((uint16_t)1u)

typedef uint8_t ITEMSTORE_ITEM_TAG_t;
enum {
  ITEMSTORE_ITEM_TAG_VALUE = 1,
  ITEMSTORE_ITEM_TAG_CODE = 2
};

typedef uint8_t ITEMSTORE_VALUE_TAG_t;
enum {
  ITEMSTORE_VALUE_TAG_INT = 0,
  ITEMSTORE_VALUE_TAG_FLOAT = 1,
  ITEMSTORE_VALUE_TAG_STRING = 2,
  ITEMSTORE_VALUE_TAG_NIL = 3,
  ITEMSTORE_VALUE_TAG_BOOL = 4
};

#define ITEMSTORE_MAX_DEPTH 8u
#define ITEMSTORE_MAX_CHILDREN_PER_ITEM 250u
#define ITEMSTORE_MAX_STRING_LEN (16u * 1024u * 1024u)
#define ITEMSTORE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)

typedef struct {
  size_t depth;
  size_t max_depth;
  uint32_t max_children_per_item;
  uint32_t max_string_len;
  uint32_t max_bytecode_len;
  const char *filename;
} ITEMSTORE_READ_CTX_t;

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

static bool read_bytes(FILE *file, void *data, size_t length,
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

static bool read_u8(FILE *file, uint8_t *value, const char *context) {
  return read_bytes(file, value, sizeof(*value), context);
}

static bool read_u16_le(FILE *file, uint16_t *value, const char *context) {
  uint8_t bytes[2];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
  return true;
}

static bool read_u32_le(FILE *file, uint32_t *value, const char *context) {
  uint8_t bytes[4];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = 0;
  for (size_t i = 0; i < sizeof(bytes); i++) {
    *value |= (uint32_t)bytes[i] << (i * 8);
  }
  return true;
}

static bool read_u64_le(FILE *file, uint64_t *value, const char *context) {
  uint8_t bytes[8];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
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
  if (depth > ITEMSTORE_MAX_DEPTH) {
    logerr("Failed to write itemstore item '%s': depth %zu exceeds maximum "
           "%u.\n", item->name, depth, ITEMSTORE_MAX_DEPTH);
    return false;
  }

  size_t name_len = strlen(item->name);
  if (name_len > 32) {
    logerr("Failed to write itemstore item '%s': name exceeds 32 bytes.\n",
           item->name);
    return false;
  }
  if (item->parent != NULL && (name_len == 0 || !is_valid_layer(item->name))) {
    logerr("Failed to write itemstore item: invalid layer name '%s'.\n",
           item->name);
    return false;
  }
  if (!write_u8(file, (uint8_t)name_len, "item name length")
      || !write_bytes(file, item->name, name_len, "item name")) {
    return false;
  }

  ITEMSTORE_ITEM_TAG_t item_tag;
  switch (item->type) {
    case ITEM_value: item_tag = ITEMSTORE_ITEM_TAG_VALUE; break;
    case ITEM_code: item_tag = ITEMSTORE_ITEM_TAG_CODE; break;
    default:
      logerr("Failed to write itemstore item '%s': unsupported item type %d.\n",
             item->name, item->type);
      return false;
  }
  if (!write_u8(file, item_tag, "item type tag")) return false;

  if (item->type == ITEM_value) {
    ITEMSTORE_VALUE_TAG_t value_tag;
    switch (item->value.type) {
      case VALUE_nil: value_tag = ITEMSTORE_VALUE_TAG_NIL; break;
      case VALUE_int: value_tag = ITEMSTORE_VALUE_TAG_INT; break;
      case VALUE_float: value_tag = ITEMSTORE_VALUE_TAG_FLOAT; break;
      case VALUE_str: value_tag = ITEMSTORE_VALUE_TAG_STRING; break;
      case VALUE_bool: value_tag = ITEMSTORE_VALUE_TAG_BOOL; break;
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
        if (length > ITEMSTORE_MAX_STRING_LEN) {
          logerr("Failed to write itemstore string payload for '%s': length "
                 "%zu exceeds maximum %u.\n", item->name, length,
                 ITEMSTORE_MAX_STRING_LEN);
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

  size_t numchildren = item->ordered_size;
  if (numchildren > ITEMSTORE_MAX_CHILDREN_PER_ITEM) {
    logerr("Failed to write itemstore item '%s': child count %zu exceeds "
           "maximum %u.\n", item->name, numchildren,
           ITEMSTORE_MAX_CHILDREN_PER_ITEM);
    return false;
  }
  if (!write_u32_le(file, (uint32_t)numchildren, "child count")) return false;

  for (size_t i = 0; i < item->ordered_size; i++) {
    if (!write_item(file, item->ordered_array[i])) return false;
  }
  return true;
}

bool save_itemstore(const char *filename, ITEM_t *root) {
  FILE *file = NULL;
  char *temp_path = NULL;
  bool success = false;
  long pid = current_process_id();
  int temp_path_len = snprintf(NULL, 0, "%s.tmp.%ld", filename, pid);
  if (temp_path_len < 0) {
    logerr("Failed to build temporary itemstore path for %s.\n", filename);
    return false;
  }

  temp_path = malloc((size_t)temp_path_len + 1);
  if (temp_path == NULL) {
    logerr("Failed to allocate temporary itemstore path for %s.\n", filename);
    return false;
  }
  snprintf(temp_path, (size_t)temp_path_len + 1, "%s.tmp.%ld", filename, pid);

  file = fopen(temp_path, "wb");
  if (file == NULL) {
    logerr("Failed to open temporary itemstore %s for writing: %s\n",
           temp_path, strerror(errno));
    goto cleanup;
  }

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

  if (itemstore_durability_requires_sync(config.itemstore_durability)
      && !itemstore_default_context()->sync_hook(file, temp_path)) {
    goto cleanup;
  }

  if (fclose(file) != 0) {
    file = NULL;
    logerr("Failed to close temporary itemstore %s: %s\n", temp_path,
           strerror(errno));
    goto cleanup;
  }
  file = NULL;

  if (rename(temp_path, filename) != 0) {
    logerr("Failed to replace itemstore %s with %s: %s\n", filename,
           temp_path, strerror(errno));
    goto cleanup;
  }

  success = true;

cleanup:
  if (file != NULL && fclose(file) != 0) {
    logerr("Failed to close temporary itemstore %s during cleanup: %s\n",
           temp_path, strerror(errno));
  }
  if (!success) {
    if (remove(temp_path) != 0 && errno != ENOENT) {
      logerr("Failed to remove temporary itemstore %s: %s\n", temp_path,
             strerror(errno));
    }
    logerr("Failed to save itemstore '%s'; existing data was not replaced.\n",
           filename);
  }
  free(temp_path);
  return success;
}

void detach_loaded_item(ITEM_t *item) {
  ITEM_t *parent = item->parent;
  if (parent != NULL) {
    delete_hashtable(parent->children, item->name);
    for (size_t i = 0; i < parent->ordered_size; i++) {
      if (parent->ordered_array[i] == item) {
        for (size_t j = i + 1; j < parent->ordered_size; j++) {
          parent->ordered_array[j - 1] = parent->ordered_array[j];
        }
        parent->ordered_size--;
        break;
      }
    }
  }
  destroy_item(item);
}

static ITEMSTORE_READ_CTX_t itemstore_read_context(const char *filename,
                                                   size_t depth) {
  ITEMSTORE_READ_CTX_t ctx = {
    .depth = depth,
    .max_depth = ITEMSTORE_MAX_DEPTH,
    .max_children_per_item = ITEMSTORE_MAX_CHILDREN_PER_ITEM,
    .max_string_len = ITEMSTORE_MAX_STRING_LEN,
    .max_bytecode_len = ITEMSTORE_MAX_BYTECODE_LEN,
    .filename = filename
  };
  return ctx;
}

/* Payload ownership remains here until make_item() accepts the record. */
static void free_unowned_item_payload(ITEM_e type, VALUE_t *value,
                                      uint8_t *bytecode) {
  if (type == ITEM_value) value_free(value);
  else free(bytecode);
}

static ITEM_t *read_item_record(FILE *file, ITEM_t *parent,
                                ITEMSTORE_READ_CTX_t *ctx) {
  char name[33];
  uint8_t name_len;
  uint8_t item_tag;
  uint8_t value_tag;
  uint64_t raw_value;
  uint32_t numchildren;
  uint8_t *bytecode = NULL;
  uint32_t bytecode_len = 0;
  VALUE_t itemval = {VALUE_nil, {0}};

  if (ctx->depth > ctx->max_depth) {
    logerr("Corrupt itemstore '%s': item depth %zu exceeds maximum %zu.\n",
           ctx->filename, ctx->depth, ctx->max_depth);
    return NULL;
  }

  if (!read_u8(file, &name_len, "item name length")) return NULL;
  if (name_len > 32) {
    logerr("Corrupt itemstore '%s': item name length %u exceeds 32 bytes "
           "at depth %zu.\n", ctx->filename, name_len, ctx->depth);
    return NULL;
  }
  if (!read_bytes(file, name, name_len, "item name")) return NULL;
  if (memchr(name, '\0', name_len) != NULL) {
    logerr("Corrupt itemstore '%s': item name contains an embedded NUL at "
           "depth %zu.\n", ctx->filename, ctx->depth);
    return NULL;
  }
  name[name_len] = '\0';
  if (parent != NULL && (name_len == 0 || !is_valid_layer(name))) {
    logerr("Corrupt itemstore '%s': invalid item layer name '%s' at depth "
           "%zu.\n", ctx->filename, name, ctx->depth);
    return NULL;
  }
  if (parent != NULL && search_hashtable(parent->children, name) != NULL) {
    logerr("Corrupt itemstore '%s': duplicate child name '%s' at depth %zu.\n",
           ctx->filename, name, ctx->depth);
    return NULL;
  }

  if (!read_u8(file, &item_tag, "item type tag")) return NULL;
  ITEM_e type;
  switch (item_tag) {
    case ITEMSTORE_ITEM_TAG_VALUE: type = ITEM_value; break;
    case ITEMSTORE_ITEM_TAG_CODE: type = ITEM_code; break;
    default:
      logerr("Corrupt itemstore '%s': unsupported item type tag %u for '%s'.\n",
             ctx->filename, item_tag, name);
      return NULL;
  }

  if (type == ITEM_value) {
    if (!read_u8(file, &value_tag, "value type tag")) return NULL;
    switch (value_tag) {
      case ITEMSTORE_VALUE_TAG_NIL: itemval.type = VALUE_nil; break;
      case ITEMSTORE_VALUE_TAG_INT: itemval.type = VALUE_int; break;
      case ITEMSTORE_VALUE_TAG_FLOAT: itemval.type = VALUE_float; break;
      case ITEMSTORE_VALUE_TAG_STRING: itemval.type = VALUE_str; break;
      case ITEMSTORE_VALUE_TAG_BOOL: itemval.type = VALUE_bool; break;
      default:
        logerr("Corrupt itemstore '%s': unsupported value type tag %u for "
               "'%s'.\n", ctx->filename, value_tag, name);
        return NULL;
    }

    switch (itemval.type) {
      case VALUE_nil:
        break;
      case VALUE_int:
        if (!read_u64_le(file, &raw_value, "integer payload")) return NULL;
        memcpy(&itemval.i, &raw_value, sizeof(itemval.i));
        break;
      case VALUE_float:
        if (!read_u64_le(file, &itemval.f_bits, "float payload")) return NULL;
        break;
      case VALUE_str:
      {
        uint32_t length;
        if (!read_u32_le(file, &length, "string length")) return NULL;
        if (length > ctx->max_string_len) {
          logerr("Corrupt itemstore '%s': string length %u for '%s' exceeds "
                 "maximum %u.\n", ctx->filename, length, name,
                 ctx->max_string_len);
          return NULL;
        }
        itemval.s = malloc((size_t)length + 1);
        if (!itemval.s) {
          logerr("Failed to load itemstore '%s': cannot allocate %u bytes "
                 "for string item '%s'.\n", ctx->filename, length, name);
          return NULL;
        }
        if (!read_bytes(file, itemval.s, length, "string payload")) {
          goto fail_before_item;
        }
        itemval.s[length] = '\0';
        break;
      }
      case VALUE_bool:
      {
        uint8_t boolean;
        if (!read_u8(file, &boolean, "boolean payload")) return NULL;
        if (boolean > 1) {
          logerr("Corrupt itemstore '%s': invalid boolean payload %u for "
                 "'%s'.\n", ctx->filename, boolean, name);
          return NULL;
        }
        itemval.i = boolean;
        break;
      }
    }
  } else {
    if (!read_u32_le(file, &bytecode_len, "bytecode length")) return NULL;
    if (bytecode_len > ctx->max_bytecode_len) {
      logerr("Corrupt itemstore '%s': bytecode length %u for '%s' exceeds "
             "maximum %u.\n", ctx->filename, bytecode_len, name,
             ctx->max_bytecode_len);
      return NULL;
    }
    if (bytecode_len > 0) {
      bytecode = malloc(bytecode_len);
      if (!bytecode) {
        logerr("Failed to load itemstore '%s': cannot allocate %u bytes for "
               "bytecode item '%s'.\n", ctx->filename, bytecode_len, name);
        return NULL;
      }
      if (!read_bytes(file, bytecode, bytecode_len, "bytecode payload")) {
        goto fail_before_item;
      }
    }

    if (config.strict_validation) {
      BC_VerifyOptions verify_options = bc_verify_strict_options();
      BC_VerifyResult verify = bc_verify_bytecode(bytecode, bytecode_len,
                                                  name, &verify_options);
      if (verify.status != BC_VERIFY_OK) {
        logerr("Corrupt itemstore '%s': bytecode verification failed for "
               "'%s': %s\n", ctx->filename, name,
               verify.diagnostic.message);
        goto fail_before_item;
      }
    }
  }

  if (!read_u32_le(file, &numchildren, "child count")) {
    goto fail_before_item;
  }
  if (numchildren > ctx->max_children_per_item) {
    logerr("Corrupt itemstore '%s': child count %u for '%s' exceeds maximum "
           "%u.\n", ctx->filename, numchildren, name,
           ctx->max_children_per_item);
    goto fail_before_item;
  }

  if (bytecode_len > (uint32_t)INT_MAX) {
    logerr("Corrupt itemstore '%s': bytecode length %u for '%s' exceeds "
           "platform item length limit.\n", ctx->filename, bytecode_len, name);
    goto fail_before_item;
  }

  ITEM_t *item = make_loaded_item(name, parent, type, itemval, bytecode,
                                  (int)bytecode_len, numchildren);

  for (uint32_t i = 0; i < numchildren; i++) {
    ctx->depth++;
    ITEM_t *child = read_item_record(file, item, ctx);
    ctx->depth--;
    if (!child) {
      detach_loaded_item(item);
      return NULL;
    }
  }
  return item;

fail_before_item:
  free_unowned_item_payload(type, &itemval, bytecode);
  return NULL;
}

ITEM_t *read_item(FILE *file, ITEM_t *parent) {
  size_t depth = 0;
  for (ITEM_t *ancestor = parent; ancestor != NULL;
       ancestor = ancestor->parent) {
    depth++;
  }
  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context("<stream>", depth);
  return read_item_record(file, parent, &ctx);
}

static bool read_itemstore_header(FILE *file, const char *filename) {
  uint8_t magic[ITEMSTORE_V1_MAGIC_SIZE];
  uint16_t version;

  if (!read_bytes(file, magic, sizeof(magic), "file-header magic")) {
    logerr("Corrupt itemstore '%s': truncated file header magic.\n", filename);
    return false;
  }
  if (memcmp(magic, ITEMSTORE_V1_MAGIC, sizeof(magic)) != 0) {
    logerr("Corrupt itemstore '%s': invalid itemstore magic.\n", filename);
    return false;
  }
  if (!read_u16_le(file, &version, "file-header version")) {
    logerr("Corrupt itemstore '%s': truncated file header version.\n",
           filename);
    return false;
  }
  if (version != ITEMSTORE_V1_FORMAT_VERSION) {
    logerr("Unsupported itemstore version in '%s': found %u, supported "
           "version is %u.\n", filename, version,
           ITEMSTORE_V1_FORMAT_VERSION);
    return false;
  }
  return true;
}

ITEM_t *load_itemstore(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    logerr("Failed to open itemstore '%s' for reading: %s\n",
           filename, strerror(errno));
    return NULL;
  }

  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context(filename, 0);
  ITEM_t *root = NULL;
  if (read_itemstore_header(file, filename)) {
    root = read_item_record(file, NULL, &ctx);
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
      logmsg("Item: %s, Value: %llu\n", currentpath,
                                     (unsigned long long)item->value.i);
    } else if (item->value.type == VALUE_str) {
      logmsg("Item: %s, Value: '%s'\n", currentpath, item->value.s);
    } else {
      logmsg("Item: %s, Value: (unknown)\n", currentpath);
    }
  }
  for (size_t i = 0; i < item->ordered_size; i++) {
    dump_item(item->ordered_array[i], currentpath, false);
  }
}
