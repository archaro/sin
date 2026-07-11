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

// The configuration object, defined in sin.c
extern CONFIG_t config;

static const char *safe_error_message(const int errnum) {
  if (errnum >= 0 && errnum < MAXERRORS && errmsg[errnum]) {
    return errmsg[errnum];
  }
  return "Unknown error";
}

static void set_string_error_field_on_root(ITEM_t *root, const char *name,
                                           const char *value) {
  VALUE_t v;
  v.type = VALUE_str;
  v.s = strdup(value ? value : "");
  set_item(root, name, v);
}


static void set_int_error_field_on_root(ITEM_t *root, const char *name,
                                        int64_t value) {
  VALUE_t v;
  v.type = VALUE_int;
  v.i = value;
  set_item(root, name, v);
}


void clear_error_item_on_root(ITEM_t *root) {
  if (!root) return;
  set_item(root, "error", VALUE_NIL);
  set_item(root, "error.msg", VALUE_NIL);
  set_item(root, "error.code", VALUE_NIL);
  set_item(root, "error.stage", VALUE_NIL);
  set_item(root, "error.file", VALUE_NIL);
  set_item(root, "error.line", VALUE_NIL);
  set_item(root, "error.column", VALUE_NIL);
  set_item(root, "error.excerpt", VALUE_NIL);
}

void clear_error_item(void) {
  clear_error_item_on_root(config.itemroot);
}

void set_error_item(const int errnum, const char *errdetail) {
  set_error_item_on_root(config.itemroot, errnum, errdetail);
}

void set_error_item_on_root(ITEM_t *root, const int errnum, const char *errdetail) {
  if (!root) return;
  // Helper function to set the error item.
  VALUE_t e, emsg;
  const char *base = safe_error_message(errnum);
  e.type = VALUE_int;
  e.i = errnum;
  set_item(root, "error", e);
  emsg.type = VALUE_str;
  if (errdetail) {
    // It's possible that there is an extended error message.
    // Allocate enough space for the two error messages, plus
    // the extra characters "errmsg (errdetail)"
    size_t elen = strlen(base) + strlen(errdetail) + 4;
    emsg.s = malloc(elen);
    snprintf(emsg.s, elen, "%s (%s)", base, errdetail);
  } else {
    emsg.s = strdup(base);
  }
  set_item(root, "error.msg", emsg);
  set_item(root, "error.code", VALUE_NIL);
  set_item(root, "error.stage", VALUE_NIL);
  set_item(root, "error.file", VALUE_NIL);
  set_item(root, "error.line", VALUE_NIL);
  set_item(root, "error.column", VALUE_NIL);
  set_item(root, "error.excerpt", VALUE_NIL);
}

void set_compiler_error_item_on_root(ITEM_t *root, const CompilerDiagnostic *diag) {
  if (!root) return;
  if (!diag) {
    set_error_item_on_root(root, ERR_COMP_UNKNOWN, NULL);
    return;
  }

  const char *stable_code = diag->stable_code
      ? diag->stable_code
      : compiler_diag_stable_code(diag->code, diag->phase);
  const char *stage = compiler_diag_phase_name(diag->phase);
  const char *file = diag->source_name ? diag->source_name : "";
  const char *message = diag->message ? diag->message : "";
  const char *excerpt = diag->excerpt ? diag->excerpt : "";
  int line = diag->has_loc ? diag->line : 0;
  int column = diag->has_loc ? diag->column : 0;

  VALUE_t e;
  e.type = VALUE_int;
  e.i = diag->code;
  set_item(root, "error", e);

  int needed = snprintf(NULL, 0,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      stable_code, stage, file, line, column, message, excerpt);
  if (needed < 0) {
    set_error_item_on_root(root, diag->code, message);
    return;
  }

  VALUE_t emsg;
  emsg.type = VALUE_str;
  emsg.s = malloc((size_t)needed + 1);
  snprintf(emsg.s, (size_t)needed + 1,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      stable_code, stage, file, line, column, message, excerpt);
  set_item(root, "error.msg", emsg);


  set_string_error_field_on_root(root, "error.code", stable_code);
  set_string_error_field_on_root(root, "error.stage", stage);
  set_string_error_field_on_root(root, "error.file", file);
  set_int_error_field_on_root(root, "error.line", line);
  set_int_error_field_on_root(root, "error.column", column);
  set_string_error_field_on_root(root, "error.excerpt", excerpt);
}

void set_compiler_error_item(const CompilerDiagnostic *diag) {
  set_compiler_error_item_on_root(config.itemroot, diag);
}
