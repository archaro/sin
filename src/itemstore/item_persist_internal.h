// Internal binary itemstore persistence interfaces.
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "item_internal.h"

/* The child-count field is a uint32 on the wire.  Resource safety is
 * enforced by the whole-file record and decode-byte budgets below. */
#define ITEMSTORE_MAX_CHILDREN_PER_ITEM UINT32_MAX
#define ITEMSTORE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)
#define ITEMSTORE_MAX_RECORDS (1024u * 1024u)
#define ITEMSTORE_MAX_DECODE_BYTES (512u * 1024u * 1024u)
#define ITEMSTORE_MAX_CONVERSION_BYTES (512u * 1024u * 1024u)

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
  size_t record_count;
  size_t max_records;
  size_t decode_bytes;
  size_t max_decode_bytes;
  bool record_budget_exhausted;
  bool decode_budget_exhausted;
  bool conversion_mode;
  char **lossy_paths;
  size_t lossy_path_count;
  size_t lossy_path_capacity;
  bool lossy_path_record_failed;
} ITEMSTORE_READ_CTX_t;

bool itemstore_read_reserve_record(ITEMSTORE_READ_CTX_t *ctx);
bool itemstore_read_charge_bytes(ITEMSTORE_READ_CTX_t *ctx, size_t amount,
                                 const char *what);
bool itemstore_valid_ref_path(const char *path, size_t length);

/* Validate a prospective mutation against the default v2 persistence
 * policy.  The payload pointers are borrowed for the duration of the call;
 * this function never consumes them or changes the tree. */
bool itemstore_validate_mutation_persistence(
    const ITEM_t *base, const char *relative_name, const ITEM_t *target,
    ITEM_e type, const VALUE_t *value, uint32_t bytecode_len,
    const uint8_t *bytecode);

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
