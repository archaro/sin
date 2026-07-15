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

bool validate_item_name(const char *item_name,
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

  const char *segment_start = item_name;
  while (true) {
    const char *dot = strchr(segment_start, '.');
    size_t layer_len = (dot != NULL) ? (size_t)(dot - segment_start)
                                     : strlen(segment_start);

    if (layer_len == 0) {
      logerr("%s called with malformed item name '%s': empty layer.\n",
                                                     func_name, item_name);
      return false;
    }
    if (layer_len > 32) {
      logerr("%s called with malformed item name '%s': layer too long.\n",
                                                     func_name, item_name);
      return false;
    }

    if (dot == NULL) return true;
    segment_start = dot + 1;
  }
}


static void create_ordered_array_with_capacity(ITEM_t *item,
                                               size_t capacity) {
  item->ordered_size = 0;
  item->ordered_capacity = capacity;
  item->ordered_array = capacity > 0
      ? (ITEM_t **)malloc(sizeof(ITEM_t *) * capacity)
      : NULL;
}

void create_ordered_array(ITEM_t *item) {
  // This must be called after a new hashtable has been created,
  // but must not be called when a hashtable is resized, or memory
  // will leak like a very leaky thing.
  create_ordered_array_with_capacity(item, ITEM_ARRAY_INIT_CAPACITY);
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
  item->parent = parent;
  item->inuse = false;
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
  strcpy(item->name, name);
  uint32_t bucket_count = presize_children
      ? hashtable_buckets_for_entries(expected_children)
      : 16u;
  size_t ordered_capacity = presize_children
      ? expected_children
      : ITEM_ARRAY_INIT_CAPACITY;
  item->children = create_hashtable((int)bucket_count);
  create_ordered_array_with_capacity(item, ordered_capacity);

  if (parent != NULL) {
    insert_hashtable(parent->children, name, item);
    parent->children = maybe_resize_hashtable(parent->children);
    resize_ordered_array(parent);
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

ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Function to insert a new item into the tree at the specified node.
  if (!value_string_within_limit(&value)) {
    logerr("insert_item called with string longer than maximum %zu bytes.\n",
           SIN_MAX_STRING_BYTES);
    value_free(&value);
    return NULL;
  }
  if (!validate_item_name(item_name, "insert_item")) {
    return NULL;
  }
  // If layers of the item don't exist, they are created with a default
  // value of 0.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  logverbose("Creating new item %s\n", item_name);
  while (current_item != NULL && *current_pos != '\0') {
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ?
                     (size_t)(next_dot - current_pos) : strlen(current_pos);
    // Copy the current layer into the buffer and null-terminate it
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0';
    // Check if the current layer exists as a child of the current item
    ITEM_t *child_item = search_hashtable(current_item->children, layer);
    if (child_item == NULL) {
      // If the child does not exist, create it with a default value of 0
      VALUE_t nil = {VALUE_nil, {0}};
      child_item = make_item(layer, current_item, ITEM_value, nil, NULL, 0);
    }
    // Move to the child item
    current_item = child_item;
    if (next_dot == NULL) {
      // If there's no next dot, we've reached the last layer
      // Possibly free currently in-use memory
      // (it might have been newly-created, or might already exist)
      if (current_item->type == ITEM_value) {
          value_free(&current_item->value);
      } else if (current_item->type == ITEM_code) {
        if (current_item->inuse) {
          char name[MAX_ITEM_NAME];
          get_itemname(current_item, name);
          logerr("Cannot delete item %s: currently in use.\n", name);
          return NULL;
        }
        if (current_item->bytecode_len > 0) {
          free(current_item->bytecode);
        }
      }
      current_item->value = value;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  itemstore_bump_generation();
  return current_item;
}

ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                      uint8_t *bytecode) {
  if (!validate_item_name(item_name, "insert_code_item")) {
    return NULL;
  }
  // This function is basically the same as insert_item() but creates a
  // code item instead of a value item.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  logverbose("Creating new item %s\n", item_name);
  while (current_item != NULL && *current_pos != '\0') {
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ?
                     (size_t)(next_dot - current_pos) : strlen(current_pos);
    // Copy the current layer into the buffer and null-terminate it
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0';
    // Check if the current layer exists as a child of the current item
    ITEM_t *child_item = search_hashtable(current_item->children, layer);
    if (child_item == NULL) {
      // If the child does not exist, create it with a default value of 0
      VALUE_t nil = {VALUE_nil, {0}};
      child_item = make_item(layer, current_item, ITEM_value, nil, NULL, 0);
    }
    // Move to the child item
    current_item = child_item;
    if (next_dot == NULL) {
      // If there's no next dot, we've reached the last layer
      // It's code item, remember!
      if (current_item->type == ITEM_value) {
        value_free(&current_item->value);
      }
      current_item->type = ITEM_code;
      current_item->value.type = VALUE_nil; // Just to be safe
      if (current_item->bytecode_len > 0) {
        free(current_item->bytecode);
      }
      current_item->bytecode_len = len;
      current_item->bytecode = bytecode;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  itemstore_bump_generation();
  return current_item;
}

ITEM_t *find_item_by_index(ITEM_t *parent, const size_t index) {
  // Given the parent item, return the indexed child.
  if (index >= parent->ordered_size) {
    // No item at that index.
    return NULL;
  }
  return parent->ordered_array[index];
}

void delete_item(ITEM_t *root, const char *item_name) {
  // Find an item and then delete it and all of its children.
  if (!validate_item_name(item_name, "delete_item")) {
    return;
  }
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    if (item->inuse) {
      char name[MAX_ITEM_NAME];
      get_itemname(item, name);
      logerr("Cannot delete item %s: currently in use.\n", name);
      return;
    }
    // We don't care about items that don't exist, just silently ignore the
    // delete request.  It's not there anyway, so why the complaining?
    // First, remove the item from its parent's hashtable:
    delete_hashtable(item->parent->children, item->name);
    // Remove from order array
    for (size_t i = 0; i < item->parent->ordered_size; i++) {
      if (item->parent->ordered_array[i] == item) {
        // Shift elements left
        for (size_t j = i; j < item->parent->ordered_size - 1; j++) {
          item->parent->ordered_array[j] = item->parent->ordered_array[j + 1];
        }
        item->parent->ordered_size--;
        break;
      }
    }
    // Now we have isolated this item, delete it and all its children.
    destroy_item(item);
    itemstore_bump_generation();
    logverbose("Item %s has been deleted, along with all of its children.\n",
                                                                 item_name);
  }
}

void set_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Find an item, and set its value.
  if (!value_string_within_limit(&value)) {
    logerr("set_item called with string longer than maximum %zu bytes.\n",
           SIN_MAX_STRING_BYTES);
    value_free(&value);
    return;
  }
  if (!validate_item_name(item_name, "set_item")) {
    return;
  }
  // If the item does not exist, it will be created, and then set.
  logverbose("Trying to set item '%s'\n", item_name);
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    // Item exists, so just update its value.
    value_replace(&item->value, value);
  } else {
    // Item doesn't exist, so create it.
    insert_item(root, item_name, value);
  }
}

static bool append_itemname(ITEM_t *item, char *itemname, size_t itemname_size) {
  if (!item || !itemname || itemname_size == 0) return false;
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
  // Courtesy of ChatGPT.
  // Layer names may be no longer than 32 characters, and may also
  // consist of characters in the set: A-Za-Z0-9_

  // Early exit if too big
  if (strlen(str) > 32) {
    return false;
  }

  // Character validation
  for (const char *p = str; *p; ++p) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) {
      return false;
    }
  }

  return true;
}
