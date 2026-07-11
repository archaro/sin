// Runtime item assignment helpers

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_item_ops.h"
#include "compiler_pipeline.h"
#include "error.h"
#include "log.h"
#include "runtime_decode.h"

#define REQUIRE_BYTES(nextop, n, opname) \
  do { if (!runtime_decode_status_ok(require_bytes(&ctx->decoder, (nextop), (n), (opname)))) return false; } while (0)

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
    ITEM_t *i = insert_item(itemroot, itemname->s, val);
    if (!i) {
      logerr("Unable to create item '%s'.\n", itemname->s);
      FREE_STR(val);
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
    if (snprintf(out_name, MAX_ITEM_NAME, "%s%s", parent, assembled_name) >= MAX_ITEM_NAME) {
      logerr("Resolved item name exceeds MAX_ITEM_NAME: %s%s\n", parent, assembled_name);
      return false;
    }
    return true;
  }

  if (snprintf(out_name, MAX_ITEM_NAME, "%s", assembled_name) >= MAX_ITEM_NAME) {
    logerr("Item name exceeds MAX_ITEM_NAME: %s\n", assembled_name);
    return false;
  }
  return true;
}

typedef struct {
  char *buf;
  uint32_t cap;
  uint32_t len;
} STRBUILDER_t;

static void sb_init(STRBUILDER_t *sb, uint32_t cap) {
  sb->buf = malloc(cap);
  sb->cap = cap;
  sb->len = 0;
  sb->buf[0] = '\0';
}

static void sb_ensure(STRBUILDER_t *sb, uint32_t add_len) {
  uint32_t need = sb->len + add_len + 1;
  if (need <= sb->cap) {
    return;
  }
  uint32_t new_cap = sb->cap;
  while (new_cap < need) {
    new_cap *= 2;
  }
  sb->buf = realloc(sb->buf, new_cap);
  sb->cap = new_cap;
}

static void sb_append_substr(STRBUILDER_t *sb, const char *src, uint32_t slen) {
  sb_ensure(sb, slen);
  memcpy(sb->buf + sb->len, src, slen);
  sb->len += slen;
  sb->buf[sb->len] = '\0';
}

static void sb_append_literal(STRBUILDER_t *sb, const char *literal) {
  sb_append_substr(sb, literal, (uint32_t)strlen(literal));
}

bool decode_assigncode_params(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in) {
  // Format assumption for params block: <u16 len><bytes> repeated, terminated by <u16 0>.
  const size_t MAX_ASSIGNCODE_PARAMS = 1024;
  const size_t MAX_ASSIGNCODE_PARAM_BYTES = 65535;
  while (1) {
    REQUIRE_BYTES(*opcodep, 2, "OP_ASSIGNCODEITEM param-len");
    uint16_t param_len = 0;
    memcpy(&param_len, *opcodep, 2);
    *opcodep += 2;
    if (param_len == 0) break;
    if (in->param_count >= MAX_ASSIGNCODE_PARAMS) return false;
    if ((in->total_param_len + param_len) > MAX_ASSIGNCODE_PARAM_BYTES) return false;
    REQUIRE_BYTES(*opcodep, param_len, "OP_ASSIGNCODEITEM param-bytes");
    char *param = malloc((size_t)param_len + 1);
    memcpy(param, *opcodep, param_len);
    param[param_len] = '\0';
    *opcodep += param_len;
    in->params = realloc((char **)in->params,
                         sizeof *in->params * (in->param_count + 1));
    in->param_lens = realloc(in->param_lens,
                             sizeof *in->param_lens * (in->param_count + 1));
    in->params[in->param_count] = param;
    in->param_lens[in->param_count] = param_len;
    in->param_count++;
    in->total_param_len += param_len;
  }
  return true;
}

bool decode_assigncode_source(RuntimeContext *ctx, uint8_t **opcodep, CODEITEM_INPUT_t *in) {
  REQUIRE_BYTES(*opcodep, 2, "OP_ASSIGNCODEITEM source-len");
  memcpy(&in->source_len, *opcodep, 2);
  *opcodep += 2;
  REQUIRE_BYTES(*opcodep, in->source_len, "OP_ASSIGNCODEITEM source-bytes");
  in->source = malloc((size_t)in->source_len + 1);
  memcpy(in->source, *opcodep, in->source_len);
  in->source[in->source_len] = '\0';
  *opcodep += in->source_len;
  return true;
}

int8_t compile_and_insert_codeitem(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in, char **errdetail) {
  ITEM_t *testitem = find_item(itemroot, itemname->s);
  if (testitem && testitem->inuse) return ERR_COMP_INUSE;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode_with_params(in->source, in->source_len, in->params, in->param_count, &out, errdetail);
  if (rc == 0 && out) {
    ptrdiff_t raw_len = out->nextbyte - out->bytecode;
    if (raw_len < 0 || (uint64_t)raw_len > UINT32_MAX) {
      rc = ERR_RUNTIME_INVALIDARGS;
    } else {
      uint32_t len = (uint32_t)raw_len;
      ITEM_t *inserted = insert_code_item(itemroot, itemname->s, len, out->bytecode);
      if (!inserted) rc = ERR_COMP_INUSE;
    }
  }
  if (out) {
    if (rc != 0 && out->bytecode) free(out->bytecode);
    free(out);
  }
  return rc;
}

void persist_codeitem_source(ITEM_t *itemroot, const VALUE_t *itemname, const CODEITEM_INPUT_t *in) {
  ITEM_t *code_item = find_item(itemroot, itemname->s);
  STRBUILDER_t sb;
  size_t source_cap = in->source_len + in->total_param_len + 16u;
  if (source_cap > UINT32_MAX) return;
  sb_init(&sb, (uint32_t)source_cap);
  if (in->param_count > 0) {
    sb_append_literal(&sb, "code {");
    for (size_t pc = 0; pc < in->param_count; pc++) {
      sb_append_literal(&sb, in->params[pc]);
      if (pc < (in->param_count - 1)) sb_append_literal(&sb, ", ");
    }
    sb_append_literal(&sb, "} (");
  } else {
    sb_append_literal(&sb, "code (");
  }
  sb_append_literal(&sb, in->source);
  sb_append_literal(&sb, ");\n");
  if (!save_itemsource(code_item, sb.buf)) {
    char fullname[MAX_ITEM_NAME];
    get_itemname(code_item, fullname);
    logerr("Source was not saved.\nItem: %s\n", fullname);
    logerr("Source:\n%s\n", sb.buf);
  }
  free(sb.buf);
}
