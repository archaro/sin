// Itemstore version 2 recursive value codec.
// Licensed under the MIT License; see LICENSE file for details.

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "item_persist_internal.h"
#include "bytecode_verify.h"
#include "item_internal.h"
#include "itemref.h"
#include "list.h"
#include "log.h"
#include "memory.h"
#include "string_limits.h"

static bool valid_ref_path(const char *path, size_t length) {
  size_t start = 0;
  size_t layers = 0;

  if (path == NULL || length == 0 || length >= MAX_ITEM_NAME ||
      path[0] == '.' || path[length - 1u] == '.' ||
      memchr(path, '\0', length) != NULL) {
    return false;
  }
  for (size_t i = 0; i <= length; i++) {
    if (i != length && path[i] != '.') {
      continue;
    }
    if (i == start || i - start > ITEM_MAX_LAYER_NAME_LENGTH ||
        ++layers > ITEM_MAX_DEPTH) {
      return false;
    }
    char layer[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
    memcpy(layer, path + start, i - start);
    layer[i - start] = '\0';
    if (!is_valid_layer(layer)) {
      return false;
    }
    start = i + 1u;
  }
  return true;
}

bool itemstore_write_v2_value(FILE *file, const VALUE_t *value, size_t depth) {
  if (file == NULL || value == NULL) {
    return false;
  }
  switch (value->type) {
    case VALUE_nil:
      return itemstore_write_u8(file, ITEMSTORE_V1_VALUE_TAG_NIL,
                                "value type tag");
    case VALUE_int: {
      uint64_t raw;
      memcpy(&raw, &value->i, sizeof raw);
      return itemstore_write_u8(file, ITEMSTORE_V1_VALUE_TAG_INT,
                                "value type tag") &&
             itemstore_write_u64_le(file, raw, "integer payload");
    }
    case VALUE_float:
      return itemstore_write_u8(file, ITEMSTORE_V1_VALUE_TAG_FLOAT,
                                "value type tag") &&
             itemstore_write_u64_le(file, value->f_bits, "float payload");
    case VALUE_str: {
      if (value->s == NULL) {
        return false;
      }
      size_t length = strlen(value->s);
      if (length > SIN_MAX_STRING_BYTES || length > UINT32_MAX) {
        logerr("Cannot write string value: length %zu exceeds limit.\n", length);
        return false;
      }
      return itemstore_write_u8(file, ITEMSTORE_V1_VALUE_TAG_STRING,
                                "value type tag") &&
             itemstore_write_u32_le(file, (uint32_t)length, "string length") &&
             itemstore_write_bytes(file, value->s, length, "string payload");
    }
    case VALUE_bool:
      if (value->i != 0 && value->i != 1) {
        return false;
      }
      return itemstore_write_u8(file, ITEMSTORE_V1_VALUE_TAG_BOOL,
                                "value type tag") &&
             itemstore_write_u8(file, (uint8_t)value->i, "boolean payload");
    case VALUE_itemref: {
      const char *path = value->itemref == NULL
          ? NULL : sin_itemref_path(value->itemref);
      size_t length = path == NULL ? 0 : strlen(path);
      if (!valid_ref_path(path, length) || length > UINT16_MAX) {
        logerr("Cannot write invalid item reference path.\n");
        return false;
      }
      return itemstore_write_u8(file, ITEMSTORE_V2_VALUE_TAG_ITEMREF,
                                "value type tag") &&
             itemstore_write_u16_le(file, (uint16_t)length,
                                    "item reference path length") &&
             itemstore_write_bytes(file, path, length,
                                   "item reference path");
    }
    case VALUE_list: {
      if (value->list == NULL || depth >= SIN_LIST_MAX_DEPTH) {
        logerr("Cannot write null or excessively nested list.\n");
        return false;
      }
      size_t count = sin_list_count(value->list);
      if (count > SIN_LIST_MAX_ELEMENTS || count > UINT32_MAX) {
        return false;
      }
      if (!itemstore_write_u8(file, ITEMSTORE_V2_VALUE_TAG_LIST,
                              "value type tag") ||
          !itemstore_write_u32_le(file, (uint32_t)count,
                                  "list element count")) {
        return false;
      }
      for (size_t i = 0; i < count; i++) {
        if (!itemstore_write_v2_value(file, sin_list_get(value->list, i),
                                      depth + 1u)) {
          return false;
        }
      }
      return true;
    }
    default:
      return false;
  }
}

static bool read_value(FILE *file, ITEMSTORE_READ_CTX_t *ctx, VALUE_t *out,
                       size_t depth) {
  uint8_t tag;
  *out = (VALUE_t){VALUE_nil, {0}};
  if (!itemstore_read_u8(file, &tag, "value type tag")) {
    return false;
  }
  switch (tag) {
    case ITEMSTORE_V1_VALUE_TAG_NIL:
      return true;
    case ITEMSTORE_V1_VALUE_TAG_INT: {
      uint64_t raw;
      if (!itemstore_read_u64_le(file, &raw, "integer payload")) {
        return false;
      }
      out->type = VALUE_int;
      memcpy(&out->i, &raw, sizeof out->i);
      return true;
    }
    case ITEMSTORE_V1_VALUE_TAG_FLOAT:
      out->type = VALUE_float;
      return itemstore_read_u64_le(file, &out->f_bits, "float payload");
    case ITEMSTORE_V1_VALUE_TAG_STRING: {
      uint32_t length;
      if (!itemstore_read_u32_le(file, &length, "string length") ||
          length > ctx->max_string_len) {
        return false;
      }
      out->s = alloc_malloc((size_t)length + 1u);
      if (out->s == NULL) {
        logerr("Failed to allocate v2 string payload.\n");
        return false;
      }
      if (!itemstore_read_bytes(file, out->s, length, "string payload") ||
          memchr(out->s, '\0', length) != NULL) {
        logerr("Corrupt v2 string payload.\n");
        free(out->s);
        out->s = NULL;
        return false;
      }
      out->s[length] = '\0';
      out->type = VALUE_str;
      return true;
    }
    case ITEMSTORE_V1_VALUE_TAG_BOOL: {
      uint8_t boolean;
      if (!itemstore_read_u8(file, &boolean, "boolean payload") ||
          boolean > 1u) {
        logerr("Corrupt v2 boolean payload.\n");
        return false;
      }
      out->type = VALUE_bool;
      out->i = boolean;
      return true;
    }
    case ITEMSTORE_V2_VALUE_TAG_ITEMREF: {
      uint16_t length;
      char *path = NULL;
      if (!itemstore_read_u16_le(file, &length,
                                 "item reference path length") ||
          length == 0 || length >= MAX_ITEM_NAME) {
        logerr("Corrupt v2 item reference path length.\n");
        return false;
      }
      path = alloc_malloc((size_t)length + 1u);
      if (path == NULL) {
        return false;
      }
      if (!itemstore_read_bytes(file, path, length, "item reference path")) {
        goto fail_ref;
      }
      path[length] = '\0';
      if (!valid_ref_path(path, length)) {
        logerr("Corrupt v2 item reference path.\n");
        goto fail_ref;
      }
      out->itemref = sin_itemref_create(path);
      if (out->itemref == NULL) {
        goto fail_ref;
      }
      out->type = VALUE_itemref;
      free(path);
      return true;
    fail_ref:
      free(path);
      return false;
    }
    case ITEMSTORE_V2_VALUE_TAG_LIST: {
      uint32_t count;
      VALUE_t *values = NULL;
      SIN_LIST_t *list = NULL;
      if (depth >= SIN_LIST_MAX_DEPTH ||
          !itemstore_read_u32_le(file, &count, "list element count") ||
          count > SIN_LIST_MAX_ELEMENTS ||
          (size_t)count > ctx->aggregate_budget) {
        logerr("Corrupt v2 list depth, count, or budget.\n");
        return false;
      }
      ctx->aggregate_budget -= count;
      if (count != 0) {
        values = alloc_calloc(count, sizeof *values);
        if (values == NULL) {
          logerr("Failed to allocate v2 list values.\n");
          ctx->aggregate_budget += count;
          return false;
        }
      }
      for (uint32_t i = 0; i < count; i++) {
        if (!read_value(file, ctx, &values[i], depth + 1u)) {
          for (uint32_t j = 0; j < i; j++) {
            value_free(&values[j]);
          }
          free(values);
          ctx->aggregate_budget += count;
          return false;
        }
      }
      list = sin_list_build_owned(values, count);
      free(values);
      if (list == NULL) {
        logerr("Failed to build v2 list value.\n");
        ctx->aggregate_budget += count;
        return false;
      }
      out->type = VALUE_list;
      out->list = list;
      return true;
    }
    default:
      logerr("Corrupt v2 unknown value tag %u.\n", tag);
      return false;
  }
}

ITEM_t *itemstore_read_v2_record(FILE *file, ITEM_t *parent,
                                 ITEMSTORE_READ_CTX_t *ctx) {
  char name[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  uint8_t name_length;
  uint8_t item_tag;
  uint32_t child_count;
  ITEM_e type;
  VALUE_t value = {VALUE_nil, {0}};
  uint8_t *bytecode = NULL;
  uint32_t bytecode_length = 0;
  ITEM_t *item = NULL;

  if (ctx->depth > ctx->max_depth ||
      !itemstore_read_u8(file, &name_length, "item name length") ||
      name_length > ITEM_MAX_LAYER_NAME_LENGTH ||
      !itemstore_read_bytes(file, name, name_length, "item name")) {
    return NULL;
  }
  if (memchr(name, '\0', name_length) != NULL) {
    logerr("Corrupt v2 item name contains NUL.\n");
    return NULL;
  }
  name[name_length] = '\0';
  if (parent != NULL && (!is_valid_layer(name) ||
                         item_children_lookup(parent->children, name) != NULL)) {
    logerr("Corrupt v2 item name or duplicate child '%s'.\n", name);
    return NULL;
  }
  if (!itemstore_read_u8(file, &item_tag, "item type tag")) {
    return NULL;
  }
  switch (item_tag) {
    case ITEMSTORE_V1_ITEM_TAG_VALUE:
      type = ITEM_value;
      if (!read_value(file, ctx, &value, 0)) {
        goto fail;
      }
      break;
    case ITEMSTORE_V1_ITEM_TAG_CODE:
      type = ITEM_code;
      if (!itemstore_read_u32_le(file, &bytecode_length, "bytecode length") ||
          bytecode_length > ctx->max_bytecode_len) {
        goto fail;
      }
      if (bytecode_length != 0) {
        bytecode = alloc_malloc(bytecode_length);
        if (bytecode == NULL ||
            !itemstore_read_bytes(file, bytecode, bytecode_length,
                                   "bytecode payload")) {
          goto fail;
        }
      }
      if (ctx->strict_validation) {
        BC_VerifyOptions options = bc_verify_strict_options();
        BC_VerifyResult verify = bc_verify_bytecode(
            bytecode, bytecode_length, name, &options);
        if (verify.status != BC_VERIFY_OK) {
          goto fail;
        }
      }
      break;
    default:
      logerr("Corrupt v2 unknown item tag %u.\n", item_tag);
      goto fail;
  }
  if (!itemstore_read_u32_le(file, &child_count, "child count") ||
      child_count > ctx->max_children_per_item || bytecode_length > INT_MAX) {
    goto fail;
  }

  ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t hook =
      itemstore_load_constructor_failure_hook();
  if (hook != NULL && hook(name)) {
    goto fail;
  }
  item = make_loaded_item(name, parent, type, (VALUE_t){VALUE_nil, {0}}, NULL,
                          (int)bytecode_length, child_count);
  if (item == NULL) {
    goto fail;
  }
  if (type == ITEM_value) {
    item->value = value;
    value = (VALUE_t){VALUE_nil, {0}};
  } else {
    item->bytecode = bytecode;
    bytecode = NULL;
  }
  for (uint32_t i = 0; i < child_count; i++) {
    ctx->depth++;
    if (itemstore_read_v2_record(file, item, ctx) == NULL) {
      ctx->depth--;
      detach_item_and_destroy(item);
      item = NULL;
      goto fail;
    }
    ctx->depth--;
  }
  return item;

fail:
  value_free(&value);
  free(bytecode);
  return NULL;
}
