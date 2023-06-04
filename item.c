// The Item.

#include <stdlib.h>
#include <string.h>

#include "item.h"
#include "memory.h"

ITEM_t *make_item(const char *name, ITEM_e type, VALUE_t val, uint8_t *code,
                                                                int len) {
  // Make a nice, shiny new item and return it.
  ITEM_t *item = NULL;
  item = GROW_ARRAY(ITEM_t, item, 0, (sizeof(ITEM_t)));
  item->name = strdup(name);
  item->type = type;
  if (type == ITEM_code) {
    // The bytecode buffer was allocated elsewhere.
    item->bytecode = code;
    item->stack = new_stack(STACK_DEFAULT_SIZE);
  } else {
    item->bytecode = NULL;
    item->val = val;
  }
  return item;
}

void free_item(ITEM_t *item) {
  if (item->type == ITEM_code) {
    free_stack(item->stack);
  }
  FREE_ARRAY(ITEM_t, item->name, strlen(item->name));
  // Even though the bytecode was allocated elsewhere, we still free it here.
  FREE_ARRAY(unsigned char, item->bytecode, item->bytecode_len);
  FREE_ARRAY(ITEM_t, item, sizeof(ITEM_t));
}
