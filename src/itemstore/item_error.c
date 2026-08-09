// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "item_internal.h"
#include "memory.h"

enum { ERROR_FIELD_COUNT = 9 };

static const char *const error_fields[ERROR_FIELD_COUNT] = {
  "error",
  "error.msg",
  "error.item",
  "error.code",
  "error.stage",
  "error.file",
  "error.line",
  "error.column",
  "error.excerpt"
};

static const char *safe_error_message(const int errnum) {
  if (errnum >= 0 && errnum < MAXERRORS && errmsg[errnum]) {
    return errmsg[errnum];
  }
  return "Unknown error";
}

static char *error_strdup(const char *s) {
  size_t allocation_size;
  if (alloc_add_overflow(strlen(s), 1u, &allocation_size)) return NULL;

  char *copy = alloc_malloc(allocation_size);
  if (copy) memcpy(copy, s, allocation_size);
  return copy;
}

static char *format_error_message(const char *base, const char *detail) {
  size_t allocation_size;
  if (detail) {
    if (alloc_add_overflow(strlen(base), strlen(detail), &allocation_size) ||
        alloc_add_overflow(allocation_size, 4u, &allocation_size)) {
      return NULL;
    }
  } else if (alloc_add_overflow(strlen(base), 1u, &allocation_size)) {
    return NULL;
  }

  char *message = alloc_malloc(allocation_size);
  if (!message) return NULL;

  int formatted = detail
      ? snprintf(message, allocation_size, "%s (%s)", base, detail)
      : snprintf(message, allocation_size, "%s", base);
  if (formatted < 0 || (size_t)formatted >= allocation_size) {
    free(message);
    return NULL;
  }
  return message;
}

static char *format_compiler_message(const char *code, const char *stage,
                                     const char *file, int line, int column,
                                     const char *message,
                                     const char *excerpt) {
  int needed = snprintf(NULL, 0,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      code, stage, file, line, column, message, excerpt);
  if (needed < 0) return NULL;

  size_t allocation_size;
  if (alloc_add_overflow((size_t)needed, 1u, &allocation_size)) return NULL;

  char *formatted_message = alloc_malloc(allocation_size);
  if (!formatted_message) return NULL;

  int formatted = snprintf(formatted_message, allocation_size,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      code, stage, file, line, column, message, excerpt);
  if (formatted < 0 || (size_t)formatted >= allocation_size) {
    free(formatted_message);
    return NULL;
  }
  return formatted_message;
}

static bool stage_value(ITEM_t *staging, const char *name, VALUE_t value) {
  ITEM_MUTATION_RESULT_t result = item_set_value(staging, name, value);
  if (!item_mutation_succeeded(result)) {
    value_free(&value);
    return false;
  }
  return true;
}

static bool stage_string(ITEM_t *staging, const char *name,
                         const char *string) {
  VALUE_t value = {
    .type = VALUE_str,
    .s = error_strdup(string ? string : "")
  };
  if (!value.s) return false;
  return stage_value(staging, name, value);
}

static ITEM_t *new_staging_item(void) {
  return make_root_item("staging");
}

typedef struct {
  ITEM_t *items[ERROR_FIELD_COUNT];
  bool complete;
} ErrorDestinations;

static bool inspect_error_destinations(ITEM_t *root,
                                       ErrorDestinations *destinations) {
  destinations->complete = true;
  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    ITEM_t *item = find_item_unchecked(root, error_fields[i]);
    if (item &&
        (item->type != ITEM_value || item->execution_pins != 0)) {
      return false;
    }
    destinations->items[i] = item;
    if (!item) destinations->complete = false;
  }
  return true;
}

static void normalize_error_destinations(
    ITEM_t *root, ErrorDestinations *destinations) {
  bool changed = false;
  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    ITEM_t *item = destinations->items[i];
    if (item && item->type == ITEM_value && item->execution_pins == 0 &&
        item->value.type != VALUE_nil) {
      value_free(&item->value);
      item->value = VALUE_NIL;
      changed = true;
    }
  }
  if (changed) itemstore_bump_payload_revision_for(root);
}

static void normalize_incomplete_error(
    ITEM_t *root, ErrorDestinations *destinations) {
  if (!destinations->complete) {
    normalize_error_destinations(root, destinations);
  }
}

static bool ensure_error_destinations(
    ITEM_t *root, ErrorDestinations *destinations) {
  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    if (!destinations->items[i]) {
      ITEM_MUTATION_RESULT_t result =
          item_set_value(root, error_fields[i], VALUE_NIL);
      if (!item_mutation_succeeded(result)) return false;
      destinations->items[i] = result.item;
    }
  }
  destinations->complete = true;
  return true;
}

static bool publish_staged_error(ITEM_t *root, ITEM_t *staging,
                                 ErrorDestinations *destinations) {
  ITEM_t *sources[ERROR_FIELD_COUNT];
  bool had_complete_topology = destinations->complete;

  /* Reject incompatible staged nodes before making topology changes. */
  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    sources[i] = find_item_unchecked(staging, error_fields[i]);
    if (!sources[i] || sources[i]->type != ITEM_value ||
        sources[i]->execution_pins != 0) {
      normalize_incomplete_error(root, destinations);
      destroy_item(staging);
      return false;
    }
  }

  if (!ensure_error_destinations(root, destinations)) {
    normalize_error_destinations(root, destinations);
    destroy_item(staging);
    return false;
  }

  /* Recheck every node before changing the first payload. */
  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    sources[i] = find_item_unchecked(staging, error_fields[i]);
    ITEM_t *destination = destinations->items[i];
    if (!sources[i] || !destination ||
        sources[i]->type != ITEM_value ||
        destination->type != ITEM_value ||
        sources[i]->execution_pins != 0 ||
        destination->execution_pins != 0) {
      if (!had_complete_topology) {
        normalize_error_destinations(root, destinations);
      }
      destroy_item(staging);
      return false;
    }
  }

  for (size_t i = 0; i < ERROR_FIELD_COUNT; i++) {
    ITEM_t *destination = destinations->items[i];
    value_free(&destination->value);
    destination->value = sources[i]->value;
    sources[i]->value = VALUE_NIL;
  }
  itemstore_bump_payload_revision_for(root);
  destroy_item(staging);
  return true;
}

void clear_error_item(ITEM_t *root) {
  if (!root) return;

  ErrorDestinations destinations;
  if (!inspect_error_destinations(root, &destinations)) return;
  if (!ensure_error_destinations(root, &destinations)) {
    normalize_error_destinations(root, &destinations);
    return;
  }
  normalize_error_destinations(root, &destinations);
}

void set_error_item(ITEM_t *root, const int errnum,
                    const char *errdetail, ITEM_t *current_item) {
  if (!root) return;

  ErrorDestinations destinations;
  if (!inspect_error_destinations(root, &destinations)) return;

  char *message = format_error_message(safe_error_message(errnum), errdetail);
  if (!message) {
    normalize_incomplete_error(root, &destinations);
    return;
  }

  char itemname[MAX_ITEM_NAME] = {0};
  if (current_item) get_itemname(current_item, itemname);

  ITEM_t *staging = new_staging_item();
  if (!staging) {
    free(message);
    normalize_incomplete_error(root, &destinations);
    return;
  }

  /* Stage the preallocated message first so stage_value always consumes it. */
  if (!stage_value(staging, "error.msg",
                   (VALUE_t){.type = VALUE_str, .s = message}) ||
      !stage_value(staging, "error",
                   (VALUE_t){.type = VALUE_int, .i = errnum}) ||
      !(current_item
        ? stage_string(staging, "error.item", itemname)
        : stage_value(staging, "error.item", VALUE_NIL)) ||
      !stage_value(staging, "error.code", VALUE_NIL) ||
      !stage_value(staging, "error.stage", VALUE_NIL) ||
      !stage_value(staging, "error.file", VALUE_NIL) ||
      !stage_value(staging, "error.line", VALUE_NIL) ||
      !stage_value(staging, "error.column", VALUE_NIL) ||
      !stage_value(staging, "error.excerpt", VALUE_NIL)) {
    destroy_item(staging);
    normalize_incomplete_error(root, &destinations);
    return;
  }
  (void)publish_staged_error(root, staging, &destinations);
}

void set_compiler_error_item(ITEM_t *root, const CompilerDiagnostic *diag) {
  if (!root) return;
  if (!diag) {
    set_error_item(root, ERR_COMP_UNKNOWN, NULL, NULL);
    return;
  }

  ErrorDestinations destinations;
  if (!inspect_error_destinations(root, &destinations)) return;

  const char *code = diag->stable_code
      ? diag->stable_code
      : compiler_diag_stable_code(diag->code, diag->phase);
  const char *stage = compiler_diag_phase_name(diag->phase);
  const char *file = diag->source_name ? diag->source_name : "";
  const char *message = diag->message ? diag->message : "";
  const char *excerpt = diag->excerpt ? diag->excerpt : "";
  int line = diag->has_loc ? diag->line : 0;
  int column = diag->has_loc ? diag->column : 0;

  char *formatted_message = format_compiler_message(
      code, stage, file, line, column, message, excerpt);
  if (!formatted_message) {
    normalize_incomplete_error(root, &destinations);
    return;
  }

  ITEM_t *staging = new_staging_item();
  if (!staging) {
    free(formatted_message);
    normalize_incomplete_error(root, &destinations);
    return;
  }

  if (!stage_value(staging, "error.msg",
                   (VALUE_t){.type = VALUE_str, .s = formatted_message}) ||
      !stage_value(staging, "error",
                   (VALUE_t){.type = VALUE_int, .i = diag->code}) ||
      !stage_value(staging, "error.item", VALUE_NIL) ||
      !stage_string(staging, "error.code", code) ||
      !stage_string(staging, "error.stage", stage) ||
      !stage_string(staging, "error.file", file) ||
      !stage_value(staging, "error.line",
                   (VALUE_t){.type = VALUE_int, .i = line}) ||
      !stage_value(staging, "error.column",
                   (VALUE_t){.type = VALUE_int, .i = column}) ||
      !stage_string(staging, "error.excerpt", excerpt)) {
    destroy_item(staging);
    normalize_incomplete_error(root, &destinations);
    return;
  }
  (void)publish_staged_error(root, staging, &destinations);
}
