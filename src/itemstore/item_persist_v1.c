// Frozen version 1 itemstore record/value codec.
// Licensed under the MIT License - see LICENSE file for details.

#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "item_persist_internal.h"
#include "bytecode_verify.h"
#include "log.h"
#include "memory.h"

static bool record_lossy_path(ITEMSTORE_READ_CTX_t *ctx, ITEM_t *item) {
  if (!ctx->conversion_mode) return true;
  char path[MAX_ITEM_NAME];
  path[0] = '\0';
  get_itemname(item, path);
  if (path[0] == '\0') {
    ctx->lossy_path_record_failed = true;
    return false;
  }
  if (ctx->lossy_path_count == ctx->lossy_path_capacity) {
    size_t required = 0;
    if (alloc_add_overflow(ctx->lossy_path_count, 1u, &required)) {
      ctx->lossy_path_record_failed = true;
      return false;
    }
    size_t new_capacity = 0;
    if (!alloc_grow_capacity(ctx->lossy_path_capacity, required,
                             &new_capacity)) {
      ctx->lossy_path_record_failed = true;
      return false;
    }
    size_t pointer_bytes = 0;
    if (alloc_mul_overflow(new_capacity, sizeof(*ctx->lossy_paths),
                           &pointer_bytes) ||
        !itemstore_read_charge_bytes(ctx, pointer_bytes,
                                     "v1 lossy-path table")) {
      ctx->lossy_path_record_failed = true;
      return false;
    }
    if (!alloc_grow_array_capacity((void **)&ctx->lossy_paths,
                                   &ctx->lossy_path_capacity,
                                   required,
                                   sizeof(*ctx->lossy_paths))) {
      ctx->lossy_path_record_failed = true;
      return false;
    }
  }
  size_t path_len = strlen(path);
  size_t allocation_size = 0;
  if (alloc_add_overflow(path_len, 1u, &allocation_size)) {
    ctx->lossy_path_record_failed = true;
    return false;
  }
  if (!itemstore_read_charge_bytes(ctx, allocation_size,
                                   "v1 lossy-path record")) {
    ctx->lossy_path_record_failed = true;
    return false;
  }
  ctx->lossy_paths[ctx->lossy_path_count] = alloc_malloc(allocation_size);
  if (ctx->lossy_paths[ctx->lossy_path_count] == NULL) {
    ctx->lossy_path_record_failed = true;
    return false;
  }
  memcpy(ctx->lossy_paths[ctx->lossy_path_count], path, path_len + 1u);
  ctx->lossy_path_count++;
  return true;
}

/* Payload ownership remains here until make_item() accepts the record. */
static void free_unowned_item_payload(ITEM_e type, VALUE_t *value,
                                      uint8_t *bytecode) {
  if (type == ITEM_value) value_free(value);
  else free(bytecode);
}

ITEM_t *itemstore_read_v1_record(FILE *file, ITEM_t *parent,
                                  ITEMSTORE_READ_CTX_t *ctx) {
  char name[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  uint8_t name_len;
  uint8_t item_tag;
  uint8_t value_tag;
  uint64_t raw_value;
  uint32_t numchildren;
  uint8_t *bytecode = NULL;
  uint32_t bytecode_len = 0;
  bool lossy_string = false;
  VALUE_t itemval = {VALUE_nil, {0}};

  if (!itemstore_read_reserve_record(ctx)) return NULL;

  if (ctx->depth > ctx->max_depth) {
    logerr("Corrupt itemstore '%s': item depth %zu exceeds maximum %zu.\n",
           ctx->filename, ctx->depth, ctx->max_depth);
    return NULL;
  }

  if (!itemstore_read_u8(file, &name_len, "item name length")) return NULL;
  if (name_len > ITEM_MAX_LAYER_NAME_LENGTH) {
    logerr("Corrupt itemstore '%s': item name length %u exceeds %u bytes "
           "at depth %zu.\n", ctx->filename, name_len,
           ITEM_MAX_LAYER_NAME_LENGTH, ctx->depth);
    return NULL;
  }
  if (!itemstore_read_bytes(file, name, name_len, "item name")) return NULL;
  if (memchr(name, '\0', name_len) != NULL) {
    logerr("Corrupt itemstore '%s': item name contains an embedded NUL at "
           "depth %zu.\n", ctx->filename, ctx->depth);
    return NULL;
  }
  name[name_len] = '\0';
  if (parent != NULL && !is_valid_layer(name)) {
    logerr("Corrupt itemstore '%s': invalid item layer name '%s' at depth "
           "%zu.\n", ctx->filename, name, ctx->depth);
    return NULL;
  }
  if (parent != NULL && item_children_lookup(parent->children, name) != NULL) {
    logerr("Corrupt itemstore '%s': duplicate child name '%s' at depth %zu.\n",
           ctx->filename, name, ctx->depth);
    return NULL;
  }

  if (!itemstore_read_u8(file, &item_tag, "item type tag")) return NULL;
  ITEM_e type;
  switch (item_tag) {
    case ITEMSTORE_V1_ITEM_TAG_VALUE: type = ITEM_value; break;
    case ITEMSTORE_V1_ITEM_TAG_CODE: type = ITEM_code; break;
    default:
      logerr("Corrupt itemstore '%s': unsupported item type tag %u for '%s'.\n",
             ctx->filename, item_tag, name);
      return NULL;
  }

  if (type == ITEM_value) {
    if (!itemstore_read_u8(file, &value_tag, "value type tag")) return NULL;
    switch (value_tag) {
      case ITEMSTORE_V1_VALUE_TAG_NIL: itemval.type = VALUE_nil; break;
      case ITEMSTORE_V1_VALUE_TAG_INT: itemval.type = VALUE_int; break;
      case ITEMSTORE_V1_VALUE_TAG_FLOAT: itemval.type = VALUE_float; break;
      case ITEMSTORE_V1_VALUE_TAG_STRING: itemval.type = VALUE_str; break;
      case ITEMSTORE_V1_VALUE_TAG_BOOL: itemval.type = VALUE_bool; break;
      default:
        logerr("Corrupt itemstore '%s': unsupported value type tag %u for "
               "'%s'.\n", ctx->filename, value_tag, name);
        return NULL;
    }

    switch (itemval.type) {
      case VALUE_nil:
        break;
      case VALUE_int:
        if (!itemstore_read_u64_le(file, &raw_value, "integer payload"))
          return NULL;
        memcpy(&itemval.i, &raw_value, sizeof(itemval.i));
        break;
      case VALUE_float:
        if (!itemstore_read_u64_le(file, &itemval.f_bits, "float payload"))
          return NULL;
        break;
      case VALUE_str:
      {
        uint32_t length;
        if (!itemstore_read_u32_le(file, &length, "string length"))
          return NULL;
        if (length > ctx->max_string_len) {
          logerr("Corrupt itemstore '%s': string length %u for '%s' exceeds "
                 "maximum %u.\n", ctx->filename, length, name,
                 ctx->max_string_len);
          return NULL;
        }
        size_t string_bytes = (size_t)length + 1u;
        if (!itemstore_read_charge_bytes(ctx, string_bytes,
                                         "v1 string payload")) {
          return NULL;
        }
        itemval.s = malloc(string_bytes);
        if (!itemval.s) {
          logerr("Failed to load itemstore '%s': cannot allocate %u bytes "
                 "for string item '%s'.\n", ctx->filename, length, name);
          return NULL;
        }
        if (!itemstore_read_bytes(file, itemval.s, length, "string payload")) {
          goto fail_before_item;
        }
        lossy_string = ctx->conversion_mode
            && memchr(itemval.s, '\0', length) != NULL;
        itemval.s[length] = '\0';
        break;
      }
      case VALUE_bool:
      {
        uint8_t boolean;
        if (!itemstore_read_u8(file, &boolean, "boolean payload")) return NULL;
        if (boolean > 1) {
          logerr("Corrupt itemstore '%s': invalid boolean payload %u for "
                 "'%s'.\n", ctx->filename, boolean, name);
          return NULL;
        }
        itemval.i = boolean;
        break;
      case VALUE_itemref:
      case VALUE_list:
      default:
        goto fail_before_item;
    }
    }
  } else {
    if (!itemstore_read_u32_le(file, &bytecode_len, "bytecode length"))
      return NULL;
    if (bytecode_len > ctx->max_bytecode_len) {
      logerr("Corrupt itemstore '%s': bytecode length %u for '%s' exceeds "
             "maximum %u.\n", ctx->filename, bytecode_len, name,
             ctx->max_bytecode_len);
      return NULL;
    }
    if (bytecode_len > 0) {
      if (!itemstore_read_charge_bytes(ctx, bytecode_len,
                                       "v1 bytecode payload")) {
        return NULL;
      }
      bytecode = malloc(bytecode_len);
      if (!bytecode) {
        logerr("Failed to load itemstore '%s': cannot allocate %u bytes for "
               "bytecode item '%s'.\n", ctx->filename, bytecode_len, name);
        return NULL;
      }
      if (!itemstore_read_bytes(file, bytecode, bytecode_len,
                                "bytecode payload")) {
        goto fail_before_item;
      }
    }

    if (ctx->strict_validation) {
      BC_VerifyResult verify = bc_verify_executable_bytecode(
          bytecode, bytecode_len, name);
      if (verify.status != BC_VERIFY_OK) {
        logerr("Corrupt itemstore '%s': bytecode verification failed for "
               "'%s': %s\n", ctx->filename, name,
               verify.diagnostic.message);
        goto fail_before_item;
      }
    }
  }

  if (!itemstore_read_u32_le(file, &numchildren, "child count")) {
    goto fail_before_item;
  }
  if (numchildren > ctx->max_children_per_item) {
    logerr("Corrupt itemstore '%s': child count %u for '%s' exceeds maximum "
           "%u.\n", ctx->filename, numchildren, name,
           ctx->max_children_per_item);
    goto fail_before_item;
  }

  size_t item_bytes = 0;
  if (!item_children_loaded_allocation_bytes(numchildren, &item_bytes) ||
      !itemstore_read_charge_bytes(ctx, item_bytes,
                                   "v1 item and child storage")) {
    goto fail_before_item;
  }

  if (bytecode_len > (uint32_t)INT_MAX) {
    logerr("Corrupt itemstore '%s': bytecode length %u for '%s' exceeds "
           "platform item length limit.\n", ctx->filename, bytecode_len, name);
    goto fail_before_item;
  }

  /* Keep payload ownership here until construction succeeds.  The
   * constructor is allowed to clean up the placeholder payload on failure,
   * while the loaded payload is adopted only by a fully constructed item. */
  VALUE_t constructor_value = itemval;
  uint8_t *constructor_bytecode = NULL;
  ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t constructor_failure_hook =
      itemstore_load_constructor_failure_hook();
  if (type == ITEM_value && constructor_value.type == VALUE_str) {
    constructor_value.s = NULL;
  }
  ITEM_t *item = NULL;
  if (constructor_failure_hook == NULL || !constructor_failure_hook(name)) {
    item = make_loaded_item(name, parent, type, constructor_value,
                            constructor_bytecode, (int)bytecode_len,
                            numchildren);
  }
  if (item == NULL) goto fail_before_item;

  if (type == ITEM_value) {
    item->value = itemval;
    itemval = (VALUE_t){VALUE_nil, {0}};
  } else {
    item->bytecode = bytecode;
    bytecode = NULL;
  }

  if (lossy_string) {
    /* This check is intentionally performed before v2 serialization. */
    if (!record_lossy_path(ctx, item)) {
      detach_item_and_destroy(item);
      return NULL;
    }
  }

  for (uint32_t i = 0; i < numchildren; i++) {
    ctx->depth++;
    ITEM_t *child = itemstore_read_v1_record(file, item, ctx);
    ctx->depth--;
    if (!child) {
      detach_item_and_destroy(item);
      return NULL;
    }
  }
  return item;

fail_before_item:
  free_unowned_item_payload(type, &itemval, bytecode);
  return NULL;
}
