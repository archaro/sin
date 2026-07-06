// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "memory.h"
#include "network.h"
#include "log.h"
#include "util.h"
#include "interpret.h"

extern CONFIG_t config;

#define OUTBUF_LENGTH 16384
#define INBUF_LENGTH 16384
#define MAX_INPUT_BUFFER_LENGTH 65536
#define MAX_INPUT_LINE_LENGTH 4096
#define MAX_OUTPUT_BUFFER_LENGTH 65536
#define MAX_OUTPUT_IN_FLIGHT_LENGTH 65536
#define MAX_OUTPUT_PENDING_LENGTH \
  (MAX_OUTPUT_BUFFER_LENGTH + MAX_OUTPUT_IN_FLIGHT_LENGTH)
#define MAX_OUTPUT_BACKPRESSURE_TICKS 128

static const telnet_telopt_t telopts[] = {
  { TELNET_TELOPT_ECHO, TELNET_WONT, TELNET_DO },
	{ -1, 0, 0 }
};

LINE_t *line;

void flush_output(LINE_t *linep);

bool validate_network_config() {
  if (config.maxconns == 0) {
    logerr("Invalid maxconns: must be greater than zero.\n");
    return false;
  }
  if (config.maxconns > (size_t)LONG_MAX) {
    logerr("Invalid maxconns: %zu exceeds maximum line number %ld.\n",
           config.maxconns, LONG_MAX);
    return false;
  }
  if (config.maxconns > SIZE_MAX / sizeof(LINE_t)) {
    logerr("Invalid maxconns: %zu is too large to allocate.\n",
           config.maxconns);
    return false;
  }
  return true;
}

void init_networking() {
  // Do that which needs to be done before starting the network interface
  if (!validate_network_config()) {
    exit(EXIT_FAILURE);
  }

  line = calloc(config.maxconns, sizeof *line);
  for (size_t l = 0; l < config.maxconns; l++) {
    line[l].linenum = l;
  }
}

LINE_t *add_line(uv_tcp_t *line_handle) {
  size_t l = 0;
  while (line[l].status != LINE_empty) {
    l++;
    if (l >= config.maxconns) {
      return NULL;
    }
  }

  write_req_t *outbuf = (write_req_t *)malloc(sizeof(write_req_t));
  char *outbase = NULL;
  write_req_t *inbuf = NULL;
  char *inbase = NULL;

  if (outbuf) {
    outbase = (char *)calloc(OUTBUF_LENGTH, 1);
  }
  if (outbuf && outbase) {
    inbuf = (write_req_t *)malloc(sizeof(write_req_t));
  }
  if (outbuf && outbase && inbuf) {
    inbase = (char *)calloc(INBUF_LENGTH, 1);
  }

  if (!outbuf || !outbase || !inbuf || !inbase) {
    logerr("Failed to allocate buffers for new connection.\n");
    free(inbase);
    free(inbuf);
    free(outbase);
    free(outbuf);
    line[l].line_handle = NULL;
    line[l].outbuf = NULL;
    line[l].inbuf = NULL;
    line[l].telnet = NULL;
    line[l].status = LINE_empty;
    return NULL;
  }

  outbuf->buf.len = 0;
  outbuf->buf.base = outbase;
  outbuf->buf.base[0] = '\0';
  outbuf->length = OUTBUF_LENGTH;

  inbuf->buf.len = 0;
  inbuf->buf.base = inbase;
  inbuf->buf.base[0] = '\0';
  inbuf->length = INBUF_LENGTH;

  line[l].line_handle = line_handle;
  line[l].status = LINE_connecting;
  line[l].telnet = NULL;
  line[l].address[0] = '\0';
  line[l].outbuf = outbuf;
  line[l].inbuf = inbuf;
  line[l].output_write_in_flight = false;
  line[l].output_in_flight_length = 0;
  line[l].output_backpressure_ticks = 0;
  line[l].input_line_length = 0;
  return &line[l];
}

static void free_client_on_close(uv_handle_t *handle) {
  free(handle);
}

void destroy_line(LINE_t *linep) {
  // Clean up the line object
  telnet_free(linep->telnet);
  free(linep->line_handle);
  if (linep->outbuf) {
    free(linep->outbuf->buf.base);
    free(linep->outbuf);
  }
  if (linep->inbuf) {
    free(linep->inbuf->buf.base);
    free(linep->inbuf);
  }
  linep->line_handle = NULL;
  linep->telnet = NULL;
  linep->outbuf = NULL;
  linep->inbuf = NULL;
  linep->output_write_in_flight = false;
  linep->output_in_flight_length = 0;
  linep->output_backpressure_ticks = 0;
  linep->input_line_length = 0;
  linep->status = LINE_empty;
}

LINE_t *find_line(uv_tcp_t *client) {
  // Given a TCP client connection, find its associated line.
  // Return the line, or NULL if not found.
  size_t l = 0;
  while (l < config.maxconns) {
    if (line[l].line_handle == client) {
      return &line[l];
    }
    l++;
  }
  // Not found!
  return NULL;
}

void client_on_close(uv_handle_t *handle) {
  LINE_t *linep = find_line((uv_tcp_t *)handle);
  if (!linep) {
    free(handle);
    return;
  }
  logmsg("Line %zu: %s disconnected.\n", linep->linenum, linep->address);
  linep->status = LINE_disconnecting;
}

static void disconnect_line_for_output_limit(LINE_t *linep, const char *reason) {
  logerr("Line %zu (%s) exceeded output limits: %s. Disconnecting.\n",
         linep->linenum, linep->address[0] ? linep->address : "unknown", reason);
  if (linep->outbuf && linep->outbuf->buf.base) {
    linep->outbuf->buf.len = 0;
    linep->outbuf->buf.base[0] = '\0';
  }
  linep->status = LINE_disconnecting;
  if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
    uv_close((uv_handle_t *)linep->line_handle, client_on_close);
  }
}

bool line_can_accept_output(LINE_t *linep, size_t len) {
  if (!linep || !linep->outbuf || !linep->outbuf->buf.base ||
      linep->status == LINE_empty || linep->status == LINE_disconnecting) {
    return false;
  }
  if (len > MAX_OUTPUT_PENDING_LENGTH) {
    return false;
  }
  size_t pending = linep->outbuf->buf.len;
  if (pending > MAX_OUTPUT_BUFFER_LENGTH ||
      linep->output_in_flight_length > MAX_OUTPUT_IN_FLIGHT_LENGTH) {
    return false;
  }
  if (pending > MAX_OUTPUT_PENDING_LENGTH - linep->output_in_flight_length) {
    return false;
  }
  size_t queued = pending + linep->output_in_flight_length;
  return queued <= MAX_OUTPUT_PENDING_LENGTH - len;
}

void append_output(LINE_t *linep, const char *msg, const ssize_t len) {
  // Append output to the buffer for this line, ready for sending later.
  if (len <= 0) return;
  size_t msg_len = (size_t)len;
  if (linep->status != LINE_empty && linep->status != LINE_disconnecting) {
    // No point in doing this if there is no connection.
    if (!linep->outbuf || !linep->outbuf->buf.base) {
      disconnect_line_for_output_limit(linep, "missing output buffer");
      return;
    }

    if (!line_can_accept_output(linep, msg_len)) {
      disconnect_line_for_output_limit(linep, "output buffer too large");
      return;
    }

    if (linep->outbuf->buf.len > SIZE_MAX - msg_len ||
        linep->outbuf->buf.len + msg_len > SIZE_MAX - 1) {
      disconnect_line_for_output_limit(linep, "output buffer length overflow");
      return;
    }

    size_t required_len = linep->outbuf->buf.len + msg_len;
    size_t required_capacity = required_len + 1;
    if (required_capacity > MAX_OUTPUT_BUFFER_LENGTH) {
      disconnect_line_for_output_limit(linep, "output buffer capacity too large");
      return;
    }

    if (required_capacity > linep->outbuf->length) {
      size_t new_capacity = ((required_capacity + OUTBUF_LENGTH - 1) / OUTBUF_LENGTH) * OUTBUF_LENGTH;
      if (new_capacity < required_capacity || new_capacity > MAX_OUTPUT_BUFFER_LENGTH) {
        new_capacity = required_capacity;
      }
      char *newbase = (char *)realloc(linep->outbuf->buf.base, new_capacity);
      if (!newbase) {
        disconnect_line_for_output_limit(linep, "output buffer allocation failed");
        return;
      }
      linep->outbuf->buf.base = newbase;
      linep->outbuf->length = new_capacity;
    }
    memcpy(linep->outbuf->buf.base + linep->outbuf->buf.len, msg, msg_len);
    linep->outbuf->buf.len = required_len;
    linep->outbuf->buf.base[linep->outbuf->buf.len] = '\0';
  }
}

static void disconnect_line_for_input_limit(LINE_t *linep, const char *reason) {
  logerr("Line %zu (%s) exceeded input limits: %s. Disconnecting.\n",
         linep->linenum, linep->address[0] ? linep->address : "unknown", reason);
  if (linep->inbuf && linep->inbuf->buf.base) {
    linep->inbuf->buf.len = 0;
    linep->inbuf->buf.base[0] = '\0';
  }
  linep->input_line_length = 0;
  linep->status = LINE_disconnecting;
  if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
    uv_close((uv_handle_t *)linep->line_handle, client_on_close);
  }
}

static bool input_line_would_exceed_limit(LINE_t *linep, const char *msg,
                                          size_t msg_len) {
  size_t current_line_length = linep->input_line_length;
  for (size_t i = 0; i < msg_len; i++) {
    if (msg[i] == '\n') {
      current_line_length = 0;
      continue;
    }
    if (current_line_length >= MAX_INPUT_LINE_LENGTH) {
      return true;
    }
    current_line_length++;
  }
  return false;
}

static size_t unterminated_input_line_length(const char *buf, size_t len) {
  for (size_t i = len; i > 0; i--) {
    if (buf[i - 1] == '\n') {
      return len - i;
    }
  }
  return len;
}

void append_input(LINE_t *linep, const char *msg, const ssize_t len) {
  // Append input to the input buffer, ready for processing later.
  // This is where telnet processing will happen.
  if (len <= 0) return;
  size_t msg_len = (size_t)len;
  if (linep->status != LINE_empty && linep->status != LINE_disconnecting) {
    if (!linep->inbuf || !linep->inbuf->buf.base) {
      disconnect_line_for_input_limit(linep, "missing input buffer");
      return;
    }

    if (msg_len > MAX_INPUT_BUFFER_LENGTH ||
        linep->inbuf->buf.len > MAX_INPUT_BUFFER_LENGTH - msg_len) {
      disconnect_line_for_input_limit(linep, "input buffer too large");
      return;
    }

    size_t required_len = linep->inbuf->buf.len + msg_len;
    if (required_len == SIZE_MAX) {
      disconnect_line_for_input_limit(linep, "input buffer length overflow");
      return;
    }
    size_t required_capacity = required_len + 1;
    if (required_capacity > MAX_INPUT_BUFFER_LENGTH) {
      disconnect_line_for_input_limit(linep, "input buffer capacity too large");
      return;
    }

    if (input_line_would_exceed_limit(linep, msg, msg_len)) {
      disconnect_line_for_input_limit(linep, "input line too long");
      return;
    }

    if (required_capacity > linep->inbuf->length) {
      size_t new_capacity = ((required_capacity + INBUF_LENGTH - 1) / INBUF_LENGTH) * INBUF_LENGTH;
      if (new_capacity < required_capacity || new_capacity > MAX_INPUT_BUFFER_LENGTH) {
        new_capacity = required_capacity;
      }
      char *newbase = (char *)realloc(linep->inbuf->buf.base, new_capacity);
      if (!newbase) {
        disconnect_line_for_input_limit(linep, "input buffer allocation failed");
        return;
      }
      linep->inbuf->buf.base = newbase;
      linep->inbuf->length = new_capacity;
    }

    memcpy(linep->inbuf->buf.base + linep->inbuf->buf.len, msg, msg_len);
    linep->inbuf->buf.len = required_len;
    linep->inbuf->buf.base[linep->inbuf->buf.len] = '\0';
    linep->input_line_length = unterminated_input_line_length(
        linep->inbuf->buf.base, linep->inbuf->buf.len);
    if (memchr(msg, '\n', msg_len)) {
      linep->status = LINE_data;
    }
  }
}

char *get_input(LINE_t *linep) {
  // Extract a line of input from the input buffer.  Should only be called
  // when the line status is LINE_data.  If there is nothing left in the
  // input buffer, set the status to LINE_idle, otherwise leave it
  // unchanged.  The buffer allocated by this function will need to be
  // freed by the calling function when it is no longer needed.
  if (!linep->inbuf || !linep->inbuf->buf.base) {
    logerr("Line %zu (%s) has no input buffer. Disconnecting.\n",
           linep->linenum, linep->address[0] ? linep->address : "unknown");
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return NULL;
  }

  char *eol = memchr(linep->inbuf->buf.base, '\n', linep->inbuf->buf.len);
  if (!eol) {
    logerr("Line %zu (%s) marked as data without a newline. Returning to idle.\n",
           linep->linenum, linep->address[0] ? linep->address : "unknown");
    linep->status = LINE_idle;
    linep->input_line_length = unterminated_input_line_length(
        linep->inbuf->buf.base, linep->inbuf->buf.len);
    return NULL;
  }

  *eol = '\0';
  char *data = strdup(linep->inbuf->buf.base);
  if (!data) {
    logerr("Failed to allocate input line for line %zu.\n", linep->linenum);
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return NULL;
  }
  // Ok, we have the line of data, now take it out of the input buffer.
  size_t consumed_len = (size_t)(eol - linep->inbuf->buf.base) + 1;
  size_t remaining_len = linep->inbuf->buf.len - consumed_len;
  size_t new_capacity = INBUF_LENGTH;
  if (remaining_len + 1 > new_capacity) {
    new_capacity = remaining_len + 1;
  }
  char *newbuffer = malloc(new_capacity);
  if (!newbuffer) {
    logerr("Failed to allocate replacement input buffer for line %zu.\n",
           linep->linenum);
    free(data);
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return NULL;
  }
  memcpy(newbuffer, eol + 1, remaining_len);
  newbuffer[remaining_len] = '\0';
  free(linep->inbuf->buf.base);
  linep->inbuf->length = new_capacity;
  linep->inbuf->buf.base = newbuffer;
  linep->inbuf->buf.len = remaining_len;
  linep->input_line_length = unterminated_input_line_length(newbuffer, remaining_len);
  if (!memchr(linep->inbuf->buf.base, '\n', linep->inbuf->buf.len)) {
    linep->status = LINE_idle;
  }
  return data;
}

void telnet_event_handler(telnet_t *telnet, telnet_event_t *ev,
                                                    		void *user_data) {
  (void)telnet;
	LINE_t *linep = (LINE_t *)user_data;
	switch (ev->type) {
	case TELNET_EV_DATA:
	  // Data received from client - process it.
    append_input(linep, ev->data.buffer, ev->data.size);
		break;
	case TELNET_EV_SEND:
	  // Data to be sent to client - process it, too.
    append_output(linep, ev->data.buffer, ev->data.size);
		break;
	case TELNET_EV_DO:
    // Here is where we negotiate requests to do something
		break;
	case TELNET_EV_ERROR:
    // If there is a telnet error, it is essentially impossible to recover.
    logerr("Telnet negotiation error.\n");
    uv_close((uv_handle_t *)linep->line_handle, client_on_close);
		break;
	default:
		// I don't know you
		break;
	}
}

static void output_write_cb(uv_write_t *req, int status) {
  write_req_t *write_req = (write_req_t *)req;
  LINE_t *linep = (LINE_t *)req->data;

  if (status < 0 && linep) {
    logerr("Line %zu (%s) write error: %s. Disconnecting.\n",
           linep->linenum, linep->address[0] ? linep->address : "unknown",
           uv_strerror(status));
  }

  free(write_req->buf.base);
  free(write_req);

  if (!linep) {
    return;
  }

  linep->output_write_in_flight = false;
  linep->output_in_flight_length = 0;
  linep->output_backpressure_ticks = 0;

  if (status < 0) {
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return;
  }

  flush_output(linep);
}

void flush_output(LINE_t *linep) {
  // Send the output to the line without reusing or mutating the write buffer
  // until libuv reports completion through output_write_cb.
  if (!linep || !linep->outbuf || !linep->outbuf->buf.base ||
      linep->status == LINE_empty || linep->status == LINE_disconnecting) {
    return;
  }

  if (linep->output_write_in_flight) {
    if (linep->outbuf->buf.len > 0 &&
        ++linep->output_backpressure_ticks > MAX_OUTPUT_BACKPRESSURE_TICKS) {
      disconnect_line_for_output_limit(linep, "output backpressured too long");
    }
    return;
  }

  if (linep->outbuf->buf.len == 0) {
    linep->output_backpressure_ticks = 0;
    return;
  }

  if (linep->outbuf->buf.len > MAX_OUTPUT_IN_FLIGHT_LENGTH) {
    disconnect_line_for_output_limit(linep, "output write too large");
    return;
  }

  write_req_t *write_req = (write_req_t *)malloc(sizeof(write_req_t));
  char *write_base = NULL;
  if (write_req) {
    write_base = (char *)malloc(linep->outbuf->buf.len);
  }
  if (!write_req || !write_base) {
    free(write_base);
    free(write_req);
    disconnect_line_for_output_limit(linep, "output write allocation failed");
    return;
  }

  memcpy(write_base, linep->outbuf->buf.base, linep->outbuf->buf.len);
  write_req->buf = uv_buf_init(write_base, (unsigned int)linep->outbuf->buf.len);
  write_req->length = linep->outbuf->buf.len;
  write_req->req.data = linep;

  linep->output_write_in_flight = true;
  linep->output_in_flight_length = linep->outbuf->buf.len;
  linep->outbuf->buf.len = 0;
  linep->outbuf->buf.base[0] = '\0';

  int err = uv_write(&write_req->req, (uv_stream_t *)linep->line_handle,
                     &write_req->buf, 1, output_write_cb);
  if (err < 0) {
    output_write_cb(&write_req->req, err);
  }
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  // Buffer for input from connected client
  (void)handle;
  buf->base = (char*)calloc(suggested_size, 1);
  if (!buf->base) {
    logerr("Failed to allocate read buffer of %zu bytes.\n", suggested_size);
    buf->len = 0;
    return;
  }
  buf->len = suggested_size;
}

void client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    if (!buf->base) {
      logerr("Read failed: input buffer allocation failed.\n");
      uv_close((uv_handle_t *) client, client_on_close);
      return;
    }
    LINE_t *linep = find_line((uv_tcp_t *)client);
    if (linep) {
      telnet_recv(linep->telnet, buf->base, nread);
    }
    free(buf->base);
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF)
      logerr("Read error %s\n", uv_err_name(nread));
    uv_close((uv_handle_t *) client, client_on_close);
  }
  free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
  (void)server;
  if (status < 0) {
    logerr("Error on new connection: %s\n", uv_strerror(status));
    return;
  }
  uv_tcp_t *client = (uv_tcp_t *) malloc(sizeof(uv_tcp_t));
  if (!client) {
    logerr("Rejected connection: failed to allocate client handle.\n");
    return;
  }

  int err = uv_tcp_init(config.loop, client);
  if (err < 0) {
    logerr("Rejected connection: uv_tcp_init failed: %s\n", uv_strerror(err));
    free(client);
    return;
  }

  err = uv_accept((uv_stream_t *)&config.listener, (uv_stream_t *)client);
  if (err < 0) {
    logerr("Rejected connection: uv_accept failed: %s\n", uv_strerror(err));
    uv_close((uv_handle_t *)client, free_client_on_close);
    return;
  }

  LINE_t *newline = add_line(client);
  if (!newline) {
    uv_buf_t gamefull = {"Too many connections.\r\n", 23};
    uv_try_write((uv_stream_t *)client, &gamefull, 1);
    logmsg("Rejected connection: maximum connections (%zu) exceeded or allocation failed.\n",
           config.maxconns);
    uv_close((uv_handle_t *)client, free_client_on_close);
    return;
  }

  newline->telnet = telnet_init(telopts, telnet_event_handler,
                                TELNET_FLAG_NVT_EOL, newline);
  if (!newline->telnet) {
    logerr("Rejected connection: telnet_init failed for line %zu.\n",
           newline->linenum);
    newline->status = LINE_disconnecting;
    uv_close((uv_handle_t *)client, client_on_close);
    return;
  }

  struct sockaddr_storage peername = {0};
  int peernamelen = sizeof(peername);
  err = uv_tcp_getpeername(client, (struct sockaddr *)&peername, &peernamelen);
  if (err < 0) {
    logerr("Rejected connection: uv_tcp_getpeername failed: %s\n",
           uv_strerror(err));
    newline->status = LINE_disconnecting;
    uv_close((uv_handle_t *)client, client_on_close);
    return;
  }

  err = uv_ip_name((struct sockaddr *)&peername, newline->address, 40);
  if (err < 0) {
    logerr("Rejected connection: uv_ip_name failed: %s\n", uv_strerror(err));
    newline->status = LINE_disconnecting;
    uv_close((uv_handle_t *)client, client_on_close);
    return;
  }

  err = uv_read_start((uv_stream_t *)client, alloc_buffer, client_read);
  if (err < 0) {
    logerr("Rejected connection: uv_read_start failed: %s\n", uv_strerror(err));
    newline->status = LINE_disconnecting;
    uv_close((uv_handle_t *)client, client_on_close);
    return;
  }

  telnet_printf(newline->telnet, "Connected.\n");
  flush_output(newline);
  logmsg("Line %zu: %s connected.\n", newline->linenum, newline->address);
}

void init_listener(uint32_t port) {
  // Use libuv to elegantly create a listener
  struct sockaddr_in6 addr;
  uv_tcp_init(config.loop, &config.listener);
  uv_ip6_addr("::", port, &addr);
  uv_tcp_bind(&config.listener, (const struct sockaddr *)&addr, 0);
  uv_tcp_nodelay(&config.listener, 1);
  int r = uv_listen((uv_stream_t *) &config.listener, 10, on_new_connection);
  if (r) {
    logerr("Failed to start listening: %s\n", uv_strerror(r));
  } else {
    logmsg("Listening on port %u.\n", port);
  }
}

void input_processor(uv_idle_t* handle) {
  // Called once per iteration of the game loop
  RuntimeContext *input_ctx = handle ? (RuntimeContext *)handle->data : NULL;
  if (!input_ctx) {
    logerr("Input runtime context is missing! Cannot continue.\n");
    exit(EXIT_FAILURE);
  }
  ITEM_t *input = find_item(input_ctx->itemroot, input_ctx->input_name);
  if (!input) {
    logerr("Input item does not exist!  Cannot continue.\n");
    exit(EXIT_FAILURE);
  }
  interpret(input_ctx, input);
  reset_stack(input_ctx->vm->stack);
  // Flush the output of every connected line
  for (size_t l = 0; l < config.maxconns; l++) {
    if (line[l].status != LINE_empty 
                                  && line[l].status != LINE_disconnecting) {
      flush_output(&line[l]);
    }
  }
  // Don't hog the CPU.
  usleep(100);
}

void shutdown_listener() {
  uv_close((uv_handle_t *)&config.listener, NULL);
}

void shutdown_networking() {
  // Having been set-up, now shut it down.  Shut it down forever.
  // All the lines will have been disconnected by this point.
  free(line);
}
