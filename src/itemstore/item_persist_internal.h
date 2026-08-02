// Internal binary itemstore persistence interfaces.
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "item_internal.h"

#define ITEMSTORE_MAX_CHILDREN_PER_ITEM 250u
#define ITEMSTORE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)

#define ITEMSTORE_V1_FORMAT_VERSION UINT16_C(1)
#define ITEMSTORE_V2_FORMAT_VERSION UINT16_C(2)
typedef uint8_t ITEMSTORE_V1_ITEM_TAG_t;
enum {
  ITEMSTORE_V1_ITEM_TAG_VALUE = 1,
  ITEMSTORE_V1_ITEM_TAG_CODE = 2
};
typedef uint8_t ITEMSTORE_V1_VALUE_TAG_t;
enum {
  ITEMSTORE_V1_VALUE_TAG_INT = 0,
  ITEMSTORE_V1_VALUE_TAG_FLOAT = 1,
  ITEMSTORE_V1_VALUE_TAG_STRING = 2,
  ITEMSTORE_V1_VALUE_TAG_NIL = 3,
  ITEMSTORE_V1_VALUE_TAG_BOOL = 4
};
enum {
  ITEMSTORE_V2_VALUE_TAG_LIST = 5,
  ITEMSTORE_V2_VALUE_TAG_ITEMREF = 6
};

typedef struct ItemstoreReadContext {
  size_t depth;
  size_t max_depth;
  uint32_t max_children_per_item;
  uint32_t max_string_len;
  uint32_t max_bytecode_len;
  const char *filename;
  bool strict_validation;
  size_t aggregate_budget;
} ITEMSTORE_READ_CTX_t;

ITEMSTORE_READ_CTX_t itemstore_read_context(const char *filename,
                                            size_t depth);
bool itemstore_read_bytes(FILE *file, void *data, size_t length,
                          const char *context);
bool itemstore_read_u8(FILE *file, uint8_t *value, const char *context);
bool itemstore_read_u16_le(FILE *file, uint16_t *value,
                           const char *context);
bool itemstore_read_u32_le(FILE *file, uint32_t *value,
                           const char *context);
bool itemstore_read_u64_le(FILE *file, uint64_t *value,
                           const char *context);
bool itemstore_write_bytes(FILE *file, const void *data, size_t length,
                           const char *context);
bool itemstore_write_u8(FILE *file, uint8_t value, const char *context);
bool itemstore_write_u16_le(FILE *file, uint16_t value, const char *context);
bool itemstore_write_u32_le(FILE *file, uint32_t value, const char *context);
bool itemstore_write_u64_le(FILE *file, uint64_t value, const char *context);
bool itemstore_write_v2_value(FILE *file, const VALUE_t *value, size_t depth,
                              size_t *aggregate_budget);
ITEM_t *itemstore_read_v1_record(FILE *file, ITEM_t *parent,
                                 ITEMSTORE_READ_CTX_t *ctx);
ITEM_t *itemstore_read_v2_record(FILE *file, ITEM_t *parent,
                                 ITEMSTORE_READ_CTX_t *ctx);
ITEM_t *itemstore_read_record_for_version(uint16_t version, FILE *file,
                                          ITEM_t *parent,
                                          ITEMSTORE_READ_CTX_t *ctx);
ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t
itemstore_load_constructor_failure_hook(void);
