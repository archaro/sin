// Runtime item assignment helpers

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "item.h"
#include "runtime_context.h"
#include "value.h"

typedef struct {
  const char **params;
  uint16_t *param_lens;
  size_t param_count;
  size_t total_param_len;
  char *source;
  uint16_t source_len;
} CODEITEM_INPUT_t;

void assignitem(ITEM_t *itemroot, VALUE_t *itemname, VALUE_t val);
bool canonicalize_itemname(const char *assembled_name, ITEM_t *context_item, char *out_name);
bool decode_assigncode_params(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in);
bool decode_assigncode_source(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in);
int8_t compile_and_insert_codeitem(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in, char **errdetail);
void persist_codeitem_source(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in, const char *srcroot);
