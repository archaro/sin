// Runtime item assignment helpers

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_item_ops.h"
#include "bytecode_wire.h"
#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "log.h"
#include "memory.h"
#include "runtime_decode.h"
#include "strbuilder.h"
#include "string_limits.h"

#define REQUIRE_BYTES(nextop, n, opname) \
  do { if (!runtime_decode_status_ok(require_bytes(&ctx->decoder, (nextop), (n), (opname)))) return false; } while (0)

static bool value_payload_owned_by_item(ITEM_t *root, const char *item_name,
                                        VALUE_t value) {
  if (!root || !item_name || value.type != VALUE_str || !value.s) return false;
  ITEM_t *target = find_item(root, item_name);
  if (!target) return false;
  if (item_kind(target) == ITEM_value) {
    const VALUE_t *stored = item_value(target);
    return stored && stored->type == VALUE_str && stored->s == value.s;
  }
  return item_kind(target) == ITEM_code &&
      item_bytecode(target) == (const uint8_t *)value.s;
}

void assignitem(ITEM_t *itemroot, VALUE_t *itemname, VALUE_t val) {
  // Given two values, use the first as the name of an item, and
  // the second as the value to assign to it.  The item name must be
  // freed after insertion.
  // If the itemname does not resolve into a valid item, this function
  // must fail silently (log messages are fine).  In this case, if the
  // value to be saved has memory allocated to it, that must be freed.
  // In other words, this is an end stage for values - they are either
  // used or discarded.  The interpreter no longer cares.
  if (itemname->type == VALUE_str) {
    ITEM_MUTATION_RESULT_t mutation =
        item_set_value(itemroot, itemname->s, val);
    if (!item_mutation_succeeded(mutation)) {
      logerr("Unable to create item '%s'.\n", itemname->s);
      if (!value_payload_owned_by_item(itemroot, itemname->s, val)) {
        FREE_STR(val);
      }
    }
    logverbose("Saved value of type %d in item %s\n", val.type, itemname->s);
  } else {
    logerr("Unable to create item: invalid name type %d\n", itemname->type);
    FREE_STR(val);
  }
  FREE_STR(*itemname);
}

bool canonicalize_itemname(const char *assembled_name, ITEM_t *context_item, char *out_name) {
  if (!assembled_name || assembled_name[0] == '\0') return false;

  if (assembled_name[0] == '.') {
    if (!context_item) {
      logerr("Relative item name '%s' cannot be resolved without current item context.\n", assembled_name);
      return false;
    }
    char parent[MAX_ITEM_NAME];
    get_itemname(context_item, parent);
    int written = snprintf(out_name, MAX_ITEM_NAME, "%s%s", parent,
                           assembled_name);
    if (written < 0 || (size_t)written >= MAX_ITEM_NAME) {
      logerr("Resolved item name exceeds MAX_ITEM_NAME: %s%s\n", parent, assembled_name);
      return false;
    }
    return true;
  }

  int written = snprintf(out_name, MAX_ITEM_NAME, "%s", assembled_name);
  if (written < 0 || (size_t)written >= MAX_ITEM_NAME) {
    logerr("Item name exceeds MAX_ITEM_NAME: %s\n", assembled_name);
    return false;
  }
  return true;
}

bool decode_assigncode_params(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in) {
  // Format assumption for params block: <u16 len><bytes> repeated, terminated by <u16 0>.
  const size_t MAX_ASSIGNCODE_PARAMS = 1024;
  while (1) {
    REQUIRE_BYTES(*opcodep, 2, "OP_ASSIGNCODEITEM param-len");
    uint16_t param_len = 0;
    param_len = bc_wire_load_u16(*opcodep);
    *opcodep += 2;
    if (param_len == 0) break;
    if (in->param_count >= MAX_ASSIGNCODE_PARAMS) return false;
    if (param_len > SIN_MAX_STRING_BYTES - in->total_param_len) return false;
    REQUIRE_BYTES(*opcodep, param_len, "OP_ASSIGNCODEITEM param-bytes");
    char *param = malloc((size_t)param_len + 1);
    if (!param) return false;
    memcpy(param, *opcodep, param_len);
    param[param_len] = '\0';
    *opcodep += param_len;
    if (!alloc_grow_array((void **)&in->params, in->param_count + 1, sizeof *in->params) ||
        !alloc_grow_array((void **)&in->param_lens, in->param_count + 1, sizeof *in->param_lens)) {
      free(param);
      return false;
    }
    in->params[in->param_count] = param;
    in->param_lens[in->param_count] = param_len;
    in->param_count++;
    in->total_param_len += param_len;
  }
  return true;
}

bool decode_assigncode_source(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in) {
  REQUIRE_BYTES(*opcodep, 2, "OP_ASSIGNCODEITEM source-len");
  in->source_len = bc_wire_load_u16(*opcodep);
  *opcodep += 2;
  REQUIRE_BYTES(*opcodep, in->source_len, "OP_ASSIGNCODEITEM source-bytes");
  in->source = malloc((size_t)in->source_len + 1);
  if (!in->source) return false;
  memcpy(in->source, *opcodep, in->source_len);
  in->source[in->source_len] = '\0';
  *opcodep += in->source_len;
  return true;
}

int8_t compile_and_insert_codeitem(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in, char **errdetail) {
  ITEM_t *testitem = find_item(itemroot, itemname->s);
  if (testitem && item_is_in_use(testitem)) return ERR_COMP_INUSE;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode_with_params(in->source, in->source_len, in->params, in->param_count, &out, errdetail);
  if (rc == 0 && out) {
    ptrdiff_t raw_len = out->nextbyte - out->bytecode;
    if (raw_len < 0 || (uint64_t)raw_len > UINT32_MAX) {
      rc = ERR_RUNTIME_INVALIDARGS;
    } else {
      uint32_t len = (uint32_t)raw_len;
      ITEM_MUTATION_RESULT_t mutation =
          item_set_code(itemroot, itemname->s, len, out->bytecode);
      if (!item_mutation_succeeded(mutation)) rc = ERR_COMP_INUSE;
    }
  }
  if (out) {
    if (rc != 0 && out->bytecode) free(out->bytecode);
    free(out);
  }
  return rc;
}

void persist_codeitem_source(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in, const char *srcroot) {
  ITEM_t *code_item = find_item(itemroot, itemname->s);
  SIN_STRBUILDER_t sb;
  size_t source_cap = in->source_len + in->total_param_len + 16u;
  if (source_cap > SIN_MAX_STRING_BYTES) source_cap = SIN_MAX_STRING_BYTES;
  if (!sin_sb_init(&sb, source_cap, SIN_MAX_STRING_BYTES)) return;
  bool assembled = true;
  if (in->param_count > 0) {
    assembled = sin_sb_append_cstr(&sb, "code {");
    for (size_t pc = 0; assembled && pc < in->param_count; pc++) {
      assembled = sin_sb_append_cstr(&sb, in->params[pc]);
      if (assembled && pc < (in->param_count - 1)) {
        assembled = sin_sb_append_cstr(&sb, ", ");
      }
    }
    if (assembled) assembled = sin_sb_append_cstr(&sb, "} (");
  } else {
    assembled = sin_sb_append_cstr(&sb, "code (");
  }
  if (assembled) assembled = sin_sb_append_cstr(&sb, in->source);
  if (assembled) assembled = sin_sb_append_cstr(&sb, ");\n");
  if (!assembled) {
    sin_sb_dispose(&sb);
    logerr("Source was not saved: assembled source exceeds maximum string size or allocation failed.\n");
    return;
  }
  char *source_text = sin_sb_take(&sb);
  if (!source_text) return;
  if (!save_itemsource_in_srcroot(code_item, source_text, srcroot)) {
    char fullname[MAX_ITEM_NAME];
    get_itemname(code_item, fullname);
    logerr("Source was not saved for item %s.\n", fullname);
    logverbose("Unsaved source:\n%s\n", source_text);
  }
  free(source_text);
}
