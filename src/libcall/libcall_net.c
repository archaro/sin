#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "network.h"
#include "stack.h"
#include "util.h"

static void lc_net_push_int(RuntimeContext *ctx, int64_t value) {
  VALUE_t ret = {VALUE_int, {.i = value}};
  push_stack(ctx->vm->stack, ret);
}

static void lc_net_set_input_line(RuntimeContext *ctx,
                                  size_t line_index) {
  VALUE_t val = {VALUE_int, {.i = (int64_t)line_index}};
  (void)item_set_value(itemstore_root(ctx->itemstore), ctx->inputline_name, val);
}

uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Advance the fair-queue cursor, publish input.line/input.text for the first
  // active connection event, and push event code 0-3.
  (void)item;
  size_t line_index = 0;
  char *input = NULL;
  NetworkEvent event = network_runtime_poll(ctx->network, &line_index, &input);
  if (event == NETWORK_EVENT_CONNECT || event == NETWORK_EVENT_DISCONNECT ||
      event == NETWORK_EVENT_DATA) {
    lc_net_set_input_line(ctx, line_index);
    if (event == NETWORK_EVENT_DATA) {
      VALUE_t str = {VALUE_str, {.s = input}};
      ITEM_MUTATION_RESULT_t mutation = item_set_value(
          itemstore_root(ctx->itemstore), ctx->inputtext_name, str);
      if (!item_mutation_succeeded(mutation)) value_free(&str);
    }
    lc_net_push_int(ctx, (int64_t)event);
    return nextop;
  }
  push_stack(ctx->vm->stack, VALUE_ZERO);
  return nextop;
}

uint8_t *lc_net_maxlines(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Return the configured bound for zero-based connection-slot enumeration.
  (void)item;
  lc_net_push_int(ctx, (int64_t)network_runtime_max_connections(ctx->network));
  return nextop;
}

uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume line and value, send value text to an active connection, and push
  // nil on success/no-op, false on output backpressure, or nil+invalidargs for
  // an invalid line argument.
  (void)item;

  VALUE_t out = pop_stack(ctx->vm->stack);
  VALUE_t linenum = pop_stack(ctx->vm->stack);

  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (size_t)linenum.i >= network_runtime_max_connections(ctx->network)) {
    VALUE_t args[] = {out, linenum};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.write line must be an integer connection index; floats are invalid");
  }

  size_t line_index = (size_t)linenum.i;
  if (!network_runtime_connected(ctx->network, line_index)) {
    FREE_STR(out);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  NetworkWriteResult write_result = NETWORK_WRITE_INACTIVE;
  char *rendered = NULL;
  size_t text_length = 0;
  VALUE_text_result_e result = value_render_text(
      &out, VALUE_TEXT_NIL_OMIT, &rendered, &text_length);
  if (result == VALUE_TEXT_OK) {
    write_result = network_runtime_write(ctx->network, line_index, rendered,
                                         text_length);
  } else if (result == VALUE_TEXT_NIL) {
    write_result = NETWORK_WRITE_SENT;
  } else {
    write_result = NETWORK_WRITE_REJECTED;
  }
  free(rendered);
  FREE_STR(out);

  if (write_result == NETWORK_WRITE_REJECTED) {
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.flush line must be a non-negative integer connection index; floats are invalid");
  }

  if ((size_t)linenum.i >= network_runtime_max_connections(ctx->network)) {
    value_free(&linenum);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_NETWORK_ERROR,
        "net.flush line is outside the configured connection range",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  size_t line_index = (size_t)linenum.i;
  value_free(&linenum);

  if (!network_runtime_flush(ctx->network, line_index)) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_NETWORK_ERROR,
        "net.flush line is not connected",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  push_stack(ctx->vm->stack, VALUE_TRUE);
  return nextop;
}

uint8_t *lc_net_echo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;

  VALUE_t enabled = pop_stack(ctx->vm->stack);
  size_t line_index = network_runtime_current_line(ctx->network);
  network_runtime_echo(ctx->network, line_index, value_is_truthy(&enabled));
  value_free(&enabled);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume a line index and request an orderly disconnect.

  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.ditch line must be a non-negative integer connection index; floats are invalid");
  }

  if ((size_t)linenum.i >= network_runtime_max_connections(ctx->network)) {
    value_free(&linenum);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_NETWORK_ERROR,
        "net.ditch line is outside the configured connection range",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  size_t line_index = (size_t)linenum.i;
  value_free(&linenum);

  if (!network_runtime_disconnect(ctx->network, line_index)) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_NETWORK_ERROR,
        "net.ditch line is not connected",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  push_stack(ctx->vm->stack, VALUE_TRUE);
  return nextop;
}

uint8_t *lc_net_connected(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (uint64_t)linenum.i > SIZE_MAX ||
      (size_t)linenum.i >= network_runtime_max_connections(ctx->network)) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.connected line must be a non-negative integer connection index within the configured range; floats are invalid");
  }

  size_t line_index = (size_t)linenum.i;
  value_free(&linenum);

  push_stack(ctx->vm->stack, network_runtime_connected(ctx->network, line_index)
                                 ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}

uint8_t *lc_net_address(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (uint64_t)linenum.i > SIZE_MAX ||
      (size_t)linenum.i >= network_runtime_max_connections(ctx->network)) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.address line must be a non-negative integer connection index within the configured range; floats are invalid");
  }

  size_t line_index = (size_t)linenum.i;
  value_free(&linenum);
  char *address = network_runtime_address(ctx->network, line_index);
  if (!address) {
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = address}});
  return nextop;
}
