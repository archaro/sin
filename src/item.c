// The Item.

#include <stdlib.h>
#include <string.h>

#include "item.h"
#include "memory.h"
#include "log.h"

ITEM_t *make_item(const char *name, ITEM_e type, VALUE_t val, uint8_t *code,
                                                                int len) {
  // Make a nice, shiny new item and return it.
  ITEM_t *item = NULL;
  item = GROW_ARRAY(ITEM_t, item, 0, 1);
  item->name = strdup(name);
  item->type = type;
  if (type == ITEM_code) {
    // The bytecode buffer was allocated elsewhere.
    item->bytecode = code;
    item->stack.max = STACK_SIZE;
    item->stack.current = -1;
  } else {
    item->bytecode = NULL;
    item->val = val;
  }
  return item;
}

void free_item(ITEM_t *item) {
  FREE_ARRAY(ITEM_t, item->name, strlen(item->name));
  // Even though the bytecode was allocated elsewhere, we still free it here.
  FREE_ARRAY(unsigned char, item->bytecode, item->bytecode_len);
  FREE_ARRAY(ITEM_t, item, sizeof(ITEM_t));
}
