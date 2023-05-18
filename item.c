// The Item.

#include <stdlib.h>
#include <string.h>

#include "item.h"

ITEM_t *make_item(char *name, ITEM_e type, VALUE_t val, char *code) {
  // Make a nice, shiny new item and return it.
  ITEM_t *item = (ITEM_t *)malloc(sizeof(ITEM_t));
  item->name = strdup(name);
  item->type = type;
  if (type == ITEM_code) {
    // The bytecode buffer was malloc()ed elsewhere.
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
    free(item->bytecode);
    free_stack(item->stack);
  }
  free(item->name);
  free(item);
}
