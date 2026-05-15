#include "test_helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"

// Tests intentionally allocate heap buffers/strings (e.g. strdup/realloc)
// to mirror production ownership boundaries; call sites free these
// allocations in the same test scope.

AS_NODE *t_int(int64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)value);
  return as_new_valnode(V_INT, strdup(buf));
}

AS_NODE *t_local(const char *name) {
  return as_new_valnode(V_LOCAL, strdup(name));
}

AS_NODE *t_node(ENUM_NODE nodetype, void *lhs, void *rhs) {
  return as_new_node(nodetype, lhs, rhs);
}

AS_NODE *t_stmtlist_with_one(AS_NODE *stmt) {
  AS_NODE *list = as_new_stmtlist_node();
  return as_stmtlist_append(list, stmt);
}

IR_Unit *t_new_unit(void) {
  return ir_create_unit();
}

void t_emit(IR_Unit *unit, IR_Inst inst) {
  (void)ir_emit(unit, inst);
}

void t_bind(IR_Unit *unit, int32_t label_id) {
  (void)ir_bind_label(unit, label_id);
}

int8_t t_emit_bytecode(IR_Unit *unit, uint8_t local_count, uint8_t param_count,
                       OUTPUT_t *out, char **errdetail) {
  return emit_bytecode(unit, local_count, param_count, out, errdetail);
}


uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
  if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
  return 0xFF;
}

uint8_t *load_hex_fixture(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    char alt[512];
    if (strncmp(path, "tests/", 6) == 0) {
      snprintf(alt, sizeof(alt), "%s", path + 6);
      f = fopen(alt, "rb");
    }
    if (!f) {
      snprintf(alt, sizeof(alt), "../%s", path);
      f = fopen(alt, "rb");
    }
  }
  ASSERT_NOT_NULL(f);
  uint8_t *buf = NULL;
  size_t cap = 0, len = 0;
  int c;
  while ((c = fgetc(f)) != EOF) {
    if (isspace(c)) continue;
    if (c == '#') {
      while ((c = fgetc(f)) != EOF && c != '\n') {
      }
      continue;
    }
    uint8_t hi = hex_nibble((char)c);
    ASSERT_TRUE(hi != 0xFF);
    int c2 = fgetc(f);
    ASSERT_TRUE(c2 != EOF);
    uint8_t lo = hex_nibble((char)c2);
    ASSERT_TRUE(lo != 0xFF);
    if (len == cap) {
      cap = cap ? cap * 2 : 32;
      buf = realloc(buf, cap);
      ASSERT_NOT_NULL(buf);
    }
    buf[len++] = (uint8_t)((hi << 4) | lo);
  }
  fclose(f);
  *out_len = len;
  return buf;
}
