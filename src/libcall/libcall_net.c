#include <string.h>

#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "network.h"
#include "stack.h"
#include "util.h"

static LibcallNetworkDeps lc_net_deps(RuntimeContext *ctx) {
  LibcallNetworkDeps net = ctx->network;
  if (!net.lines) net.lines = line;
  if (!net.maxconns) net.maxconns = ctx->maxconns;
  if (!net.lastconn) net.lastconn = ctx->lastconn;
  if (!net.inputline_name) net.inputline_name = ctx->inputline_name;
  if (!net.inputtext_name) net.inputtext_name = ctx->inputtext_name;
  return net;
}

static void lc_net_push_int(RuntimeContext *ctx, int64_t value) {
  VALUE_t ret = {VALUE_int, {.i = value}};
  push_stack(ctx->vm->stack, ret);
}

static void lc_net_set_input_line(RuntimeContext *ctx,
                                  const LibcallNetworkDeps *net,
                                  size_t line_index) {
  VALUE_t val = {VALUE_int, {.i = (int64_t)line_index}};
  set_item(ctx->itemroot, net->inputline_name, val);
}

static bool lc_net_line_can_write(const LINE_t *linep) {
  return linep->telnet != NULL &&
         (linep->status == LINE_data || linep->status == LINE_idle);
}

static bool lc_net_send_text(LINE_t *linep, size_t line_index,
                             const char *text, size_t len) {
  if (linep->outbuf && !line_can_accept_output(linep, len)) {
    logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
           line_index);
    return false;
  }
  telnet_send_text(linep->telnet, text, len);
  return true;
}

uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Advance the fair-queue cursor, publish input.line/input.text for the first
  // active connection event, and push event code 0-3.
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  size_t *lastconn = deps.lastconn;
  size_t maxconns = *deps.maxconns;
  (void)item;

  (*lastconn)++;
  if (maxconns == 0 || *lastconn >= maxconns) {
    *lastconn = 0;
  }
  while (*lastconn < maxconns) {
    size_t line_index = *lastconn;
    LINE_t *linep = &deps.lines[line_index];

    switch (linep->status) {
      case LINE_connecting:
        linep->status = LINE_idle;
        lc_net_set_input_line(ctx, &deps, line_index);
        lc_net_push_int(ctx, 1);
        return nextop;
      case LINE_disconnecting:
        destroy_line(linep);
        linep->status = LINE_empty;
        lc_net_set_input_line(ctx, &deps, line_index);
        lc_net_push_int(ctx, 2);
        return nextop;
      case LINE_data: {
        lc_net_set_input_line(ctx, &deps, line_index);
        VALUE_t str = {VALUE_str, {0}};
        str.s = get_input(linep);
        set_item(ctx->itemroot, deps.inputtext_name, str);
        lc_net_push_int(ctx, 3);
        return nextop;
      }
      default:
        (*lastconn)++;
    }
  }
  push_stack(ctx->vm->stack, VALUE_ZERO);
  return nextop;
}

uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume line and value, send value text to an active connection, and push
  // nil on success/no-op, false on output backpressure, or nil+invalidargs for
  // an invalid line argument.
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  (void)item;

  VALUE_t out = pop_stack(ctx->vm->stack);
  VALUE_t linenum = pop_stack(ctx->vm->stack);

  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (size_t)linenum.i >= *deps.maxconns) {
    VALUE_t args[] = {out, linenum};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.write line must be an integer connection index; floats are invalid");
  }

  size_t line_index = (size_t)linenum.i;
  LINE_t *linep = &deps.lines[line_index];
  if (!lc_net_line_can_write(linep)) {
    FREE_STR(out);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  bool sent = true;
  char buffer[VALUE_PLAIN_TEXT_BUFFER_SIZE];
  const char *text = NULL;
  size_t text_length = 0;
  VALUE_text_result_e result = value_plain_text(
      &out, VALUE_TEXT_NIL_OMIT, buffer, sizeof(buffer), &text, &text_length);
  if (result == VALUE_TEXT_OK) {
    sent = lc_net_send_text(linep, line_index, text, text_length);
  }
  FREE_STR(out);

  if (!sent || linep->status == LINE_disconnecting) {
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.flush line must be a non-negative integer connection index; floats are invalid");
  }

  if ((size_t)linenum.i >= *deps.maxconns) {
    value_free(&linenum);
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_NETWORK_ERROR,
        "net.flush line is outside the configured connection range",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  size_t line_index = (size_t)linenum.i;
  LINE_t *linep = &deps.lines[line_index];
  value_free(&linenum);

  if (!line_is_active(linep)) {
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_NETWORK_ERROR,
        "net.flush line is not connected",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  flush_output(linep);
  push_stack(ctx->vm->stack, VALUE_TRUE);
  return nextop;
}

uint8_t *lc_net_echo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  (void)item;

  VALUE_t enabled = pop_stack(ctx->vm->stack);
  size_t line_index = *deps.lastconn;
  if (line_index < *deps.maxconns) {
    LINE_t *linep = &deps.lines[line_index];
    if (lc_net_line_can_write(linep)) {
      unsigned char command = value_is_truthy(&enabled) ? TELNET_WONT : TELNET_WILL;
      telnet_negotiate(linep->telnet, command, TELNET_TELOPT_ECHO);
    }
  }
  value_free(&enabled);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume line.  If line is not an integer >= 0, return nil.
  // If line is currently in any state other than LINE_empty or
  // LINE_disconnecting, set its state to LINE_disconnecting and call uv_close.
  // Return true if successful, otherwise false.  If false, set the error item
  // appropriately with errno=ERR_NETWORK_ERROR

  LibcallNetworkDeps deps = lc_net_deps(ctx);
  (void)item;

  VALUE_t linenum = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0) {
    value_free(&linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.ditch line must be a non-negative integer connection index; floats are invalid");
  }

  if ((size_t)linenum.i >= *deps.maxconns) {
    value_free(&linenum);
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_NETWORK_ERROR,
        "net.ditch line is outside the configured connection range",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  size_t line_index = (size_t)linenum.i;
  LINE_t *linep = &deps.lines[line_index];
  value_free(&linenum);

  if (!line_is_active(linep)) {
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_NETWORK_ERROR,
        "net.ditch line is not connected",
        ctx ? ctx->current_item : NULL);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  request_line_disconnect(linep);
  push_stack(ctx->vm->stack, VALUE_TRUE);
  return nextop;
}
