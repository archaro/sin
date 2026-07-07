#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "floatconv.h"
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

uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  LibcallNetworkDeps *net = &deps;
  // Called by the task which checks for player input.
  // We operate a fair queuing process here.  Everyone
  // gets a turn.  Find the next activity.
  (void)item;
  (*net->lastconn)++;
  if ((*net->maxconns) == 0 || (*net->lastconn) >= (*net->maxconns)) {
    (*net->lastconn) = 0;
  }
  while ((*net->lastconn) < (*net->maxconns)) {
    VALUE_t val = {VALUE_int, {0}};
    // Find some activity.
    switch (net->lines[(*net->lastconn)].status) {
      case LINE_connecting:
        net->lines[(*net->lastconn)].status = LINE_idle;
        // Set the input item to the current line
        val.i = (long)(*net->lastconn);
        set_item(ctx->itemroot, net->inputline_name, val);
        // And return a value from this libcall to say what happened.
        val.i = 1;
        push_stack(ctx->vm->stack, val);
        return nextop;
      case LINE_disconnecting:
        destroy_line(&net->lines[(*net->lastconn)]);
        net->lines[(*net->lastconn)].status = LINE_empty;
        // Set the input item to the current line
        val.i = (long)(*net->lastconn);
        set_item(ctx->itemroot, net->inputline_name, val);
        val.i = 2;
        push_stack(ctx->vm->stack, val);
        return nextop;
      case LINE_data:
        // Set the input item to the current line
        val.i = (long)(*net->lastconn);
        set_item(ctx->itemroot, net->inputline_name, val);
        // And grab some data.
        VALUE_t str = {VALUE_str, {0}};
        str.s = get_input(&net->lines[(*net->lastconn)]);
        set_item(ctx->itemroot, net->inputtext_name, str);
        val.i = 3;
        push_stack(ctx->vm->stack, val);
        return nextop;
      default:
        (*net->lastconn)++;
    }
  }
  // No activity found.
  push_stack(ctx->vm->stack, VALUE_ZERO);
  return nextop;
}

uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  LibcallNetworkDeps deps = lc_net_deps(ctx);
  LibcallNetworkDeps *net = &deps;
  // Write data out to a line
  // Validate the parameters before creating the task.
  (void)item;
  VALUE_t out = pop_stack(ctx->vm->stack);
  VALUE_t linenum = pop_stack(ctx->vm->stack);
  size_t line_index = 0;

  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (size_t)linenum.i >= (*net->maxconns)) {
    FREE_STR(out);
    FREE_STR(linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.write line must be an integer connection index; floats are invalid");
  } else {
    line_index = (size_t)linenum.i;
    if ((net->lines[line_index].status != LINE_data
        && net->lines[line_index].status != LINE_idle)
        || net->lines[line_index].telnet == NULL) {
      FREE_STR(out);
      push_stack(ctx->vm->stack, VALUE_NIL);
      return nextop;
    }
    switch(out.type) {
      case VALUE_str:
        if (net->lines[line_index].outbuf &&
            !line_can_accept_output(&net->lines[line_index], strlen(out.s))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          FREE_STR(out);
          push_stack(ctx->vm->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(net->lines[line_index].telnet, out.s, strlen(out.s));
        FREE_STR(out);
        break;
      case VALUE_int: {
        char buffer[22];
        snprintf(buffer, sizeof(buffer), "%" PRId64, out.i);
        if (net->lines[line_index].outbuf &&
            !line_can_accept_output(&net->lines[line_index], strlen(buffer))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          push_stack(ctx->vm->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(net->lines[line_index].telnet, buffer, strlen(buffer));
        break;
      }
      case VALUE_float: {
        char fbuffer[64];
        if (sin_format_binary64_buf(out.f, fbuffer, sizeof(fbuffer))) {
          if (net->lines[line_index].outbuf &&
              !line_can_accept_output(&net->lines[line_index], strlen(fbuffer))) {
            logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                   line_index);
            push_stack(ctx->vm->stack, VALUE_FALSE);
            return nextop;
          }
          telnet_send_text(net->lines[line_index].telnet, fbuffer, strlen(fbuffer));
        }
        break;
      }
      case VALUE_nil:
        // Nothing to output
        break;
      case VALUE_bool: {
        char *t = "true";
        char *f = "false";
        if (net->lines[line_index].outbuf &&
            !line_can_accept_output(&net->lines[line_index], strlen(out.i?t:f))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          push_stack(ctx->vm->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(net->lines[line_index].telnet, out.i?t:f,
                                                        strlen(out.i?t:f));
        break;
      }
    }
  }
  if (net->lines[line_index].status == LINE_disconnecting) {
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }
  // Libcalls always return a value
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}
