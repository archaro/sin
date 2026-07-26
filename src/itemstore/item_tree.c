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
#include "string_limits.h"

// The configuration object, defined in src/sin.c
extern CONFIG_t config;

static ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t item_creation_failure_hook;

bool item_layer_char_is_allowed(unsigned char character) {
  return (character >= 'A' && character <= 'Z')
      || (character >= 'a' && character <= 'z')
      || (character >= '0' && character <= '9')
      || character == '_';
}

static bool item_base_path_info(const ITEM_t *base, size_t *depth,
                                size_t *name_length, const char *func_name) {
  size_t base_depth = 0;
  size_t base_name_length = 0;

  for (const ITEM_t *current = base; current != NULL;
       current = current->parent) {
    size_t layer_length = strnlen(current->name,
                                  ITEM_MAX_LAYER_NAME_LENGTH + 1u);
    if (layer_length > ITEM_MAX_LAYER_NAME_LENGTH) {
      logerr("%s called with malformed ancestor item name: layer exceeds "
             "%u bytes.\n", func_name, ITEM_MAX_LAYER_NAME_LENGTH);
      return false;
    }
    if (current->parent == NULL) {
      break;
    }
    if (!is_valid_layer(current->name)) {
      logerr("%s called relative to malformed ancestor item '%s'.\n",
             func_name, current->name);
      return false;
    }
    if (base_depth > 0) base_name_length++;
    base_name_length += layer_length;
    base_depth++;
  }

  *depth = base_depth;
  *name_length = base_name_length;
  return true;
}

bool validate_item_name_relative(const ITEM_t *base, const char *item_name,
                                 const char *func_name) {
  /*
   * Item names are already-assembled strings, not numeric values. Integer
   * layers may be assembled by the compiler/runtime using base-10 integer
   * text, but float values are rejected before this API is called. Therefore
   * this validator treats dots only as layer separators: "1.0" is the two
   * layers "1" and "0", not the float spellings "1.0" or "1.00"; +0.0/-0.0
   * and NaN payloads have no item-name representation.
   */
  if (!item_name || *item_name == '\0') {
    logerr("%s called with empty item name.\n", func_name);
    return false;
  }

  size_t depth;
  size_t base_name_length;
  if (!item_base_path_info(base, &depth, &base_name_length, func_name)) {
    return false;
  }

  size_t path_length = 0;
  size_t layer_length = 0;
  for (const unsigned char *current = (const unsigned char *)item_name;;
       current++) {
    unsigned char character = *current;
    if (character != '\0' && character != '.') {
      path_length++;
      layer_length++;
      if (!item_layer_char_is_allowed(character)) {
        logerr("%s called with malformed item name '%s': character 0x%02x "
               "is not allowed.\n", func_name, item_name, character);
        return false;
      }
      if (layer_length > ITEM_MAX_LAYER_NAME_LENGTH) {
        logerr("%s called with malformed item name '%s': layer exceeds %u "
               "bytes.\n", func_name, item_name,
               ITEM_MAX_LAYER_NAME_LENGTH);
        return false;
      }
      if (base_name_length + (base != NULL && base->parent != NULL ? 1u : 0u)
          + path_length > ITEM_MAX_FULL_NAME_LENGTH) {
        logerr("%s called with malformed item name '%s': complete name "
               "exceeds %u bytes.\n", func_name, item_name,
               ITEM_MAX_FULL_NAME_LENGTH);
        return false;
      }
      continue;
    }

    if (layer_length == 0) {
      logerr("%s called with malformed item name '%s': empty layer.\n",
             func_name, item_name);
      return false;
    }
    depth++;
    if (depth > ITEM_MAX_DEPTH) {
      logerr("%s called with malformed item name '%s': depth exceeds %u "
             "non-root layers.\n", func_name, item_name, ITEM_MAX_DEPTH);
      return false;
    }
    if (character == '\0') return true;
    path_length++;
    layer_length = 0;
  }
}

bool validate_item_name(const char *item_name, const char *func_name) {
  return validate_item_name_relative(NULL, item_name, func_name);
}


static bool create_ordered_array_with_capacity(ITEM_t *item,
                                               size_t capacity) {
  item->ordered_size = 0;
  item->ordered_capacity = capacity;
  item->ordered_array = capacity > 0
      ? (ITEM_t **)malloc(sizeof(ITEM_t *) * capacity)
      : NULL;
  if (capacity > 0 && !item->ordered_array) {
    item->ordered_capacity = 0;
    return false;
  }
  return true;
}

bool create_ordered_array(ITEM_t *item) {
  // This must be called after a new hashtable has been created,
  // but must not be called when a hashtable is resized, or memory
  // will leak like a very leaky thing.
  return create_ordered_array_with_capacity(item, ITEM_ARRAY_INIT_CAPACITY);
}

static uint32_t hashtable_buckets_for_entries(uint32_t entry_count) {
  if (entry_count == 0) return 1;
  return (uint32_t)(((uint64_t)entry_count * 4u + 2u) / 3u);
}

static ITEM_t *construct_item(const char *name, ITEM_t *parent, ITEM_e type,
                              VALUE_t value, uint8_t *bytecode, int len,
                              uint32_t expected_children,
                              bool presize_children) {
  ITEM_t *item = allocate_item();
  if (!item) return NULL;
  item->parent = parent;
  item->store = parent ? parent->store : NULL;
  item->execution_pins = 0;
  item->type = type;
  item->bytecode = NULL;
  item->bytecode_len = 0;
  // There are two types of items.  Those which don't contain a value
  // MUST contain bytecode.
  if (type == ITEM_value) {
    item->value = value;
  } else {
    // The bytecode is allocated elsewhere, before calling this function.
    item->bytecode = bytecode;
    item->bytecode_len = len < 0 ? 0u : (uint32_t)len;
  }
  int name_len = snprintf(item->name, sizeof item->name, "%s", name);
  if (name_len < 0 || (size_t)name_len >= sizeof item->name) {
    if (type == ITEM_value) value_free(&value);
    else free(bytecode);
    deallocate_item(item);
    return NULL;
  }
  uint32_t bucket_count = presize_children
      ? hashtable_buckets_for_entries(expected_children)
      : 16u;
  size_t ordered_capacity = presize_children
      ? expected_children
      : ITEM_ARRAY_INIT_CAPACITY;
  item->children = create_hashtable((int)bucket_count);
  if (!item->children ||
      !create_ordered_array_with_capacity(item, ordered_capacity)) {
    free_hashtable(item->children);
    if (type == ITEM_value) value_free(&item->value);
    else free(item->bytecode);
    deallocate_item(item);
    return NULL;
  }

  if (parent != NULL) {
    if (!insert_hashtable(parent->children, name, item)) {
      destroy_item(item);
      return NULL;
    }
    parent->children = maybe_resize_hashtable(parent->children);
    if (!resize_ordered_array(parent)) {
      delete_hashtable(parent->children, name);
      destroy_item(item);
      return NULL;
    }
    if (!parent->ordered_array || parent->ordered_size >= parent->ordered_capacity) {
      delete_hashtable(parent->children, name);
      destroy_item(item);
      return NULL;
    }
    parent->ordered_array[parent->ordered_size++] = item;
  }
  return item;
}

ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len) {
  // Note that for performance reasons this function does not check
  // to see if the item already exists at this layer.  You MUST
  // check that before you call this function!
  return construct_item(name, parent, type, value, bytecode, len, 0, false);
}

ITEM_t *make_root_item(const char* name) {
  VALUE_t value = {.type = VALUE_int, .i = 0};
  return construct_item(name, NULL, ITEM_value, value, NULL, 0, 0, false);
}

ITEM_t *make_loaded_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len,
                                uint32_t expected_children) {
  return construct_item(name, parent, type, value, bytecode, len,
                        expected_children, true);
}

void destroy_item(ITEM_t *item) {
  if (!item) return;
  if (item->parent == NULL) {
    itemstore_invalidate_cache_for(item);
  }
  if (item->type == ITEM_code) {
    free(item->bytecode);
  } else if (item->type == ITEM_value) {
    value_free(&item->value);
  }
  // Free the item's innards
  free_hashtable(item->children);
  free(item->ordered_array);
  // Then free the item
  deallocate_item(item);
}

void itemstore_set_item_creation_failure_hook_for_tests(
    ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t hook) {
  item_creation_failure_hook = hook;
}

void detach_item_and_destroy(ITEM_t *item) {
  if (!item) return;

  ITEM_t *parent = item->parent;
  if (parent) {
    delete_hashtable(parent->children, item->name);
    for (size_t i = 0; i < parent->ordered_size; i++) {
      if (parent->ordered_array[i] != item) continue;
      for (size_t j = i; j + 1 < parent->ordered_size; j++) {
        parent->ordered_array[j] = parent->ordered_array[j + 1];
      }
      parent->ordered_size--;
      break;
    }
  }
  destroy_item(item);
}

static ITEM_t *find_or_create_item(ITEM_t *root, const char *item_name,
                                   const char *func_name,
                                   ITEM_t **created_root) {
  if (created_root) *created_root = NULL;
  if (!validate_item_name_relative(root, item_name, func_name)) return NULL;

  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  char layer[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  logverbose("Creating new item %s\n", item_name);
  while (current_item != NULL && *current_pos != '\0') {
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ?
                     (size_t)(next_dot - current_pos) : strlen(current_pos);
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0';

    ITEM_t *child_item = search_hashtable(current_item->children, layer);
    if (child_item == NULL) {
      if (item_creation_failure_hook && item_creation_failure_hook(layer)) {
        logerr("Unable to create item '%s': failed to create layer '%s'.\n",
               item_name, layer);
        if (created_root && *created_root) {
          detach_item_and_destroy(*created_root);
          *created_root = NULL;
        }
        return NULL;
      }
      VALUE_t nil = {VALUE_nil, {0}};
      child_item = make_item(layer, current_item, ITEM_value, nil, NULL, 0);
      if (child_item == NULL) {
        logerr("Unable to create item '%s': failed to create layer '%s'.\n",
               item_name, layer);
        if (created_root && *created_root) {
          detach_item_and_destroy(*created_root);
          *created_root = NULL;
        }
        return NULL;
      }
      if (created_root && !*created_root) *created_root = child_item;
    }

    current_item = child_item;
    if (next_dot == NULL) return current_item;
    current_pos = next_dot + 1;
  }
  return NULL;
}

// Cross-kind aliases are included only for failure cleanup: replacement
// helpers reject them before either payload can be freed or adopted.
static bool value_aliases_owned_payload(const ITEM_t *item,
                                        const VALUE_t *value) {
  return item && value && value->type == VALUE_str && value->s &&
      ((item->type == ITEM_value && item->value.type == VALUE_str &&
        item->value.s == value->s) ||
       (item->type == ITEM_code &&
        (uint8_t *)item->bytecode == (uint8_t *)value->s));
}

static bool value_aliases_code_payload(const ITEM_t *item,
                                       const VALUE_t *value) {
  return item && value && value->type == VALUE_str && value->s &&
      item->type == ITEM_code &&
      (uint8_t *)item->bytecode == (uint8_t *)value->s;
}

static bool bytecode_aliases_value_payload(const ITEM_t *item,
                                           const uint8_t *bytecode) {
  return item && bytecode && item->type == ITEM_value &&
      item->value.type == VALUE_str &&
      (uint8_t *)item->value.s == bytecode;
}

static void log_incompatible_payload_alias(const ITEM_t *item) {
  char name[MAX_ITEM_NAME];
  get_itemname((ITEM_t *)item, name);
  logerr("Cannot replace item %s: incoming payload aliases an incompatible "
         "existing payload.\n", name);
}

static bool item_replacement_allowed(ITEM_t *item) {
  if (item->execution_pins == 0) return true;

  char name[MAX_ITEM_NAME];
  get_itemname(item, name);
  logerr("Cannot replace item %s: currently in use.\n", name);
  return false;
}

static bool replace_item_value(ITEM_t *item, VALUE_t value) {
  if (!item_replacement_allowed(item)) return false;
  if (value_aliases_code_payload(item, &value)) {
    log_incompatible_payload_alias(item);
    return false;
  }

  if (!(value.type == VALUE_str && item->type == ITEM_value &&
        item->value.type == VALUE_str && item->value.s == value.s)) {
    if (item->type == ITEM_value) {
      value_free(&item->value);
    } else {
      free(item->bytecode);
    }
  }
  item->bytecode = NULL;
  item->bytecode_len = 0;
  item->type = ITEM_value;
  item->value = value;
  return true;
}

static bool replace_item_code(ITEM_t *item, uint32_t len, uint8_t *bytecode) {
  if (!item_replacement_allowed(item)) return false;
  if (bytecode_aliases_value_payload(item, bytecode)) {
    log_incompatible_payload_alias(item);
    return false;
  }

  bool aliases_old_payload = item->type == ITEM_code &&
                             item->bytecode == bytecode;
  if (!aliases_old_payload) {
    if (item->type == ITEM_value) {
      value_free(&item->value);
    } else {
      free(item->bytecode);
    }
  }
  item->type = ITEM_code;
  item->value = VALUE_NIL;
  item->bytecode_len = len;
  item->bytecode = bytecode;
  return true;
}

ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  ITEM_t *created_root = NULL;
  ITEM_t *item = find_or_create_item(root, item_name, "insert_item",
                                     &created_root);
  if (!item) return NULL;
  if (value_aliases_code_payload(item, &value)) {
    log_incompatible_payload_alias(item);
    detach_item_and_destroy(created_root);
    return NULL;
  }
  if (!value_string_within_limit(&value)) {
    logerr("insert_item called with string longer than maximum %zu bytes.\n",
           SIN_MAX_STRING_BYTES);
    detach_item_and_destroy(created_root);
    return NULL;
  }
  if (!replace_item_value(item, value)) {
    detach_item_and_destroy(created_root);
    return NULL;
  }

  if (created_root) {
    itemstore_bump_topology_revision_for(root);
  } else {
    itemstore_bump_payload_revision_for(root);
  }
  return item;
}

ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                      uint8_t *bytecode) {
  ITEM_t *created_root = NULL;
  ITEM_t *item = find_or_create_item(root, item_name, "insert_code_item",
                                     &created_root);
  if (!item) return NULL;
  if (bytecode_aliases_value_payload(item, bytecode)) {
    log_incompatible_payload_alias(item);
    detach_item_and_destroy(created_root);
    return NULL;
  }
  if (!replace_item_code(item, len, bytecode)) {
    detach_item_and_destroy(created_root);
    return NULL;
  }

  if (created_root) {
    itemstore_bump_topology_revision_for(root);
  } else {
    itemstore_bump_payload_revision_for(root);
  }
  return item;
}

ITEM_t *find_item_by_index(ITEM_t *parent, const size_t index) {
  // Given the parent item, return the indexed child.
  if (index >= parent->ordered_size) {
    // No item at that index.
    return NULL;
  }
  return parent->ordered_array[index];
}

static bool item_subtree_has_execution_pins(const ITEM_t *item) {
  if (item->execution_pins != 0) return true;
  for (size_t i = 0; i < item->ordered_size; i++) {
    if (item_subtree_has_execution_pins(item->ordered_array[i])) return true;
  }
  return false;
}

void delete_item(ITEM_t *root, const char *item_name) {
  // Find an item and then delete it and all of its children.
  if (!validate_item_name_relative(root, item_name, "delete_item")) {
    return;
  }
  ITEM_t *item = find_item_unchecked(root, item_name);
  if (item) {
    bool subtree_in_use = item_subtree_has_execution_pins(item);
    if (subtree_in_use) {
      char name[MAX_ITEM_NAME];
      get_itemname(item, name);
      logerr("Cannot delete item %s: item or descendant currently in use.\n",
             name);
      return;
    }
    // We don't care about items that don't exist, just silently ignore the
    // delete request.  It's not there anyway, so why the complaining?
    detach_item_and_destroy(item);
    itemstore_bump_topology_revision_for(root);
    logverbose("Item %s has been deleted, along with all of its children.\n",
                                                                 item_name);
  }
}

void set_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  ITEM_t *created_root = NULL;
  ITEM_t *item = find_or_create_item(root, item_name, "set_item",
                                     &created_root);
  if (!item) {
    value_free(&value);
    return;
  }
  if (value_aliases_code_payload(item, &value)) {
    log_incompatible_payload_alias(item);
    detach_item_and_destroy(created_root);
    return;
  }
  if (!value_string_within_limit(&value)) {
    logerr("set_item called with string longer than maximum %zu bytes.\n",
           SIN_MAX_STRING_BYTES);
    value_free(&value);
    detach_item_and_destroy(created_root);
    return;
  }
  if (!replace_item_value(item, value)) {
    if (!value_aliases_owned_payload(item, &value)) value_free(&value);
    detach_item_and_destroy(created_root);
    return;
  }

  if (created_root) {
    itemstore_bump_topology_revision_for(root);
  } else {
    itemstore_bump_payload_revision_for(root);
  }
}

static bool append_itemname(ITEM_t *item, char *itemname, size_t itemname_size) {
  if (!item || !itemname || itemname_size == 0) return false;
  if (!item->parent) {
    int written = snprintf(itemname, itemname_size, "%s", item->name);
    return written >= 0 && (size_t)written < itemname_size;
  }
  if (item->parent->parent) {
    if (!append_itemname(item->parent, itemname, itemname_size)) return false;
    size_t used = strlen(itemname);
    if (used >= itemname_size) return false;
    int written = snprintf(itemname + used, itemname_size - used, ".%s", item->name);
    return written >= 0 && (size_t)written < itemname_size - used;
  }

  int written = snprintf(itemname, itemname_size, "%s", item->name);
  return written >= 0 && (size_t)written < itemname_size;
}

void get_itemname(ITEM_t *item, char *itemname) {
  // Returns the full name of an item.  The itemname buffer must be
  // at least MAX_ITEM_NAME in length.
  if (!append_itemname(item, itemname, MAX_ITEM_NAME)) {
    if (itemname) itemname[0] = '\0';
    logerr("Unable to assemble item name within MAX_ITEM_NAME.\n");
  }
}

char *get_itemfilename_in_srcroot(ITEM_t *item, const char *srcroot) {
  // Returns the filename of the item (only relevant if it is a source
  // item).  The return value will need to be freed by the caller.
  char *filename, *p;
  char itemname[MAX_ITEM_NAME];
  size_t l;

  if (!srcroot) srcroot = "";
  itemname[0] = '\0';
  get_itemname(item, itemname);
  l = strlen(itemname) + strlen(srcroot) + 13;
  filename = malloc((size_t)l);
  if (!filename) return NULL;
  p = itemname;
  while (*p) {
    if(*p == '.') *p = '/';
    p++;
  }
  snprintf(filename, l, "%s/%s/source.sin", srcroot, itemname);
  return filename;
}

char *get_itemfilename(ITEM_t *item) {
  return get_itemfilename_in_srcroot(item, config.srcroot);
}

bool is_valid_layer(const char *str) {
  if (!str || *str == '\0') return false;
  size_t length = 0;
  for (const unsigned char *p = (const unsigned char *)str;; p++) {
    if (*p == '\0') return true;
    if (length >= ITEM_MAX_LAYER_NAME_LENGTH
        || !item_layer_char_is_allowed(*p)) return false;
    length++;
  }
}
