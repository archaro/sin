// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>

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
static size_t network_maxconns;

static NetworkRuntimeDeps *deps_from_server(uv_stream_t *server) {
  return (server && server->data) ? (NetworkRuntimeDeps *)server->data : NULL;
}

static void close_line_handle(LINE_t *linep) {
  if (linep && linep->line_handle && !linep->close_requested &&
      !uv_is_closing((uv_handle_t *)linep->line_handle)) {
    linep->close_requested = true;
    uv_close((uv_handle_t *)linep->line_handle, client_on_close);
  }
}

bool line_is_active(const LINE_t *linep) {
  return linep && (linep->status == LINE_connecting ||
                   linep->status == LINE_idle ||
                   linep->status == LINE_data);
}

bool line_is_disconnect_pending(const LINE_t *linep) {
  return linep && linep->status == LINE_disconnecting;
}

bool line_is_disconnected(const LINE_t *linep) {
  return linep && linep->status == LINE_empty;
}

bool line_is_reusable(const LINE_t *linep) {
  return line_is_disconnected(linep) &&
         linep->line_handle == NULL &&
         linep->telnet == NULL &&
         linep->outbuf == NULL &&
         linep->inbuf == NULL &&
         !linep->output_write_in_flight &&
         linep->output_in_flight_length == 0 &&
         linep->output_backpressure_ticks == 0 &&
         !linep->close_after_output &&
         (!linep->close_requested ||
          (linep->close_completed && linep->disconnect_event_delivered)) &&
         linep->input_line_length == 0;
}

bool validate_network_deps(const NetworkRuntimeDeps *deps) {
  if (!deps || !deps->loop || !deps->listener) {
    logerr("Invalid network runtime dependencies.\n");
    return false;
  }
  size_t maxconns = deps ? deps->maxconns : 0;
  if (maxconns == 0) {
    logerr("Invalid maxconns: must be greater than zero.\n");
    return false;
  }
  if (maxconns > (size_t)LONG_MAX) {
    logerr("Invalid maxconns: %zu exceeds maximum line number %ld.\n",
           maxconns, LONG_MAX);
    return false;
  }
  if (maxconns > SIZE_MAX / sizeof(LINE_t)) {
    logerr("Invalid maxconns: %zu is too large to allocate.\n",
           maxconns);
    return false;
  }
  return true;
}

bool init_networking_with_deps(NetworkRuntimeDeps *deps) {
  // Do that which needs to be done before starting the network interface
  if (!validate_network_deps(deps)) {
    return false;
  }

  LINE_t **lines = deps->lines ? deps->lines : &line;
  LINE_t *new_lines = calloc(deps->maxconns, sizeof *new_lines);
  if (!new_lines) {
    logerr("Failed to allocate network line state.\n");
    return false;
  }
  *lines = new_lines;
  line = *lines;
  network_maxconns = deps->maxconns;
  for (size_t l = 0; l < deps->maxconns; l++) {
    (*lines)[l].linenum = l;
  }
  return true;
}

LINE_t *add_line(uv_tcp_t *line_handle) {
  size_t l = 0;
  while (!line_is_reusable(&line[l])) {
    l++;
    if (l >= (network_maxconns ? network_maxconns : config.maxconns)) {
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
  line[l].close_after_output = false;
  line[l].close_requested = false;
  line[l].close_completed = false;
  line[l].disconnect_event_delivered = false;
  line[l].input_line_length = 0;
  return &line[l];
}

static void free_client_on_close(uv_handle_t *handle) {
  free(handle);
}

static void release_line_resources(LINE_t *linep) {
  if (linep->telnet) {
    telnet_free(linep->telnet);
  }
  if (linep->outbuf) {
    free(linep->outbuf->buf.base);
    free(linep->outbuf);
  }
  if (linep->inbuf) {
    free(linep->inbuf->buf.base);
    free(linep->inbuf);
  }
  linep->telnet = NULL;
  linep->outbuf = NULL;
  linep->inbuf = NULL;
  linep->output_write_in_flight = false;
  linep->output_in_flight_length = 0;
  linep->output_backpressure_ticks = 0;
  linep->close_after_output = false;
  linep->input_line_length = 0;
}

void destroy_line(LINE_t *linep) {
  // Public teardown is only valid after the TCP close callback and writes.
  if (!linep || linep->line_handle || linep->output_write_in_flight) {
    return;
  }
  release_line_resources(linep);
  if (linep->close_completed) {
    linep->disconnect_event_delivered = true;
  }
  linep->status = LINE_empty;
}

LINE_t *find_line(uv_tcp_t *client) {
  // Given a TCP client connection, find its associated line.
  // Return the line, or NULL if not found.
  size_t l = 0;
  while (l < (network_maxconns ? network_maxconns : config.maxconns)) {
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
  /* libuv owns a live handle until this callback; this is its sole release. */
  free(handle);
  linep->line_handle = NULL;
  linep->close_completed = true;
  logmsg("Line %zu: %s disconnected.\n", linep->linenum, linep->address);
  linep->status = LINE_disconnecting;
  if (!linep->output_write_in_flight) {
    release_line_resources(linep);
  }
}

void close_network_handle(uv_handle_t *handle) {
  if (!handle || uv_is_closing(handle)) return;
  uv_close(handle, find_line((uv_tcp_t *)handle) ? client_on_close : NULL);
}

void network_close_walk_cb(uv_handle_t *handle, void *arg) {
  (void)arg;
  close_network_handle(handle);
}

static void disconnect_line_for_output_limit(LINE_t *linep, const char *reason) {
  logerr("Line %zu (%s) exceeded output limits: %s. Disconnecting.\n",
         linep->linenum, linep->address[0] ? linep->address : "unknown", reason);
  if (linep->outbuf && linep->outbuf->buf.base) {
    linep->outbuf->buf.len = 0;
    linep->outbuf->buf.base[0] = '\0';
  }
  linep->status = LINE_disconnecting;
  close_line_handle(linep);
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

void append_output(LINE_t *linep, const char *msg, size_t len) {
  // Append output to the buffer for this line, ready for sending later.
  if (len == 0) return;
  size_t msg_len = len;
  if (line_is_active(linep)) {
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
  close_line_handle(linep);
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

void append_input(LINE_t *linep, const char *msg, size_t len) {
  // Append input to the input buffer, ready for processing later.
  // This is where telnet processing will happen.
  if (len == 0) return;
  size_t msg_len = len;
  if (line_is_active(linep)) {
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
    close_line_handle(linep);
    return NULL;
  }

  char *eol = memchr(linep->inbuf->buf.base, '\n', linep->inbuf->buf.len);
  if (!eol) {
    logverbose("Line %zu (%s) marked as data without a newline. Returning to idle.\n",
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
    close_line_handle(linep);
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
    close_line_handle(linep);
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
    linep->status = LINE_disconnecting;
    close_line_handle(linep);
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

  if (linep->close_completed) {
    release_line_resources(linep);
    return;
  }

  if (status < 0) {
    linep->status = LINE_disconnecting;
    close_line_handle(linep);
    return;
  }

  if (linep->close_after_output) {
    if (linep->outbuf && linep->outbuf->buf.len > 0) {
      flush_output(linep);
      return;
    }
    linep->status = LINE_disconnecting;
    close_line_handle(linep);
    return;
  }

  flush_output(linep);
}

void flush_output(LINE_t *linep) {
  // Send the output to the line without reusing or mutating the write buffer
  // until libuv reports completion through output_write_cb.
  bool draining_disconnect =
      linep && linep->status == LINE_disconnecting && linep->close_after_output;
  if (!linep || !linep->outbuf || !linep->outbuf->buf.base ||
      line_is_disconnected(linep) ||
      (line_is_disconnect_pending(linep) && !draining_disconnect)) {
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

void request_line_disconnect(LINE_t *linep) {
  if (!line_is_active(linep)) {
    return;
  }

  linep->close_after_output = true;
  flush_output(linep);
  linep->status = LINE_disconnecting;
  if (!linep->output_write_in_flight &&
      (!linep->outbuf || linep->outbuf->buf.len == 0)) {
    close_line_handle(linep);
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
    if (line_is_active(linep)) {
      telnet_recv(linep->telnet, buf->base, (size_t)nread);
    }
    free(buf->base);
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF)
      logerr("Read error %s\n", uv_err_name((int)nread));
    LINE_t *linep = find_line((uv_tcp_t *)client);
    if (linep) {
      linep->status = LINE_disconnecting;
      close_line_handle(linep);
    } else if (!uv_is_closing((uv_handle_t *)client)) {
      uv_close((uv_handle_t *)client, client_on_close);
    }
  }
  free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    logerr("Error on new connection: %s\n", uv_strerror(status));
    return;
  }
  NetworkRuntimeDeps *deps = deps_from_server(server);
  if (!deps || !deps->loop || !deps->listener) {
    logerr("Rejected connection: network runtime dependencies are missing.\n");
    return;
  }
  uv_tcp_t *client = (uv_tcp_t *) malloc(sizeof(uv_tcp_t));
  if (!client) {
    logerr("Rejected connection: failed to allocate client handle.\n");
    return;
  }

  int err = uv_tcp_init(deps->loop, client);
  if (err < 0) {
    logerr("Rejected connection: uv_tcp_init failed: %s\n", uv_strerror(err));
    free(client);
    return;
  }

  err = uv_accept(server, (uv_stream_t *)client);
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
           (network_maxconns ? network_maxconns : config.maxconns));
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

static void close_listener_handle(uv_tcp_t *listener, bool *initialized) {
  if (!listener || !initialized || !*initialized) return;
  if (!uv_is_closing((uv_handle_t *)listener)) {
    uv_close((uv_handle_t *)listener, NULL);
  }
  *initialized = false;
}

static bool init_ipv4_listener(NetworkRuntimeDeps *deps, uv_tcp_t *listener,
                               bool *initialized, uint16_t port) {
  struct sockaddr_in addr;
  int err = uv_ip4_addr("0.0.0.0", (int)port, &addr);
  if (err < 0) {
    logerr("Failed to create IPv4 listener address: %s\n", uv_strerror(err));
    return false;
  }
  err = uv_tcp_init(deps->loop, listener);
  if (err < 0) {
    logerr("Failed to initialize IPv4 listener: %s\n", uv_strerror(err));
    return false;
  }
  *initialized = true;
  listener->data = deps;
  err = uv_tcp_bind(listener, (const struct sockaddr *)&addr, 0);
  if (err < 0) {
    logerr("Failed to bind IPv4 listener: %s\n", uv_strerror(err));
    return false;
  }
  err = uv_tcp_nodelay(listener, 1);
  if (err < 0) {
    logerr("Failed to configure IPv4 listener: %s\n", uv_strerror(err));
    return false;
  }
  err = uv_listen((uv_stream_t *)listener, 10, on_new_connection);
  if (err < 0) {
    logerr("Failed to listen on IPv4 listener: %s\n", uv_strerror(err));
    return false;
  }
  return true;
}

static bool get_listener_port(uv_tcp_t *listener, int family,
                              uint16_t *port) {
  struct sockaddr_storage bound = {0};
  int bound_len = (int)sizeof(bound);
  int err = uv_tcp_getsockname(listener, (struct sockaddr *)&bound,
                               &bound_len);
  if (err != 0 || bound.ss_family != family) return false;
  if (family == AF_INET6) {
    *port = ntohs(((struct sockaddr_in6 *)&bound)->sin6_port);
  } else {
    *port = ntohs(((struct sockaddr_in *)&bound)->sin_port);
  }
  return true;
}

bool init_listener_with_deps(NetworkRuntimeDeps *deps, uint32_t port) {
  if (!deps || !deps->loop || !deps->listener) {
    logerr("Invalid listener runtime dependencies.\n");
    return false;
  }
  if (port > UINT16_MAX) {
    logerr("Invalid listener port %u.\n", port);
    return false;
  }

  /* Binding IPv4 first for port 0 avoids platform-specific failures when a
   * just-selected IPv6 ephemeral port cannot be reused by a second socket. */
  if (port == 0 && deps->listener_ipv4) {
    if (!init_ipv4_listener(deps, deps->listener_ipv4,
                            &deps->listener_ipv4_initialized, 0)) {
      close_listener_handle(deps->listener_ipv4,
                            &deps->listener_ipv4_initialized);
      return false;
    }
    uint16_t selected_port = 0;
    if (!get_listener_port(deps->listener_ipv4, AF_INET, &selected_port)) {
      logerr("Failed to determine the ephemeral listener port.\n");
      close_listener_handle(deps->listener_ipv4,
                            &deps->listener_ipv4_initialized);
      return false;
    }
    struct sockaddr_in6 ephemeral_addr6;
    int ephemeral_err = uv_ip6_addr("::", (int)selected_port,
                                    &ephemeral_addr6);
    if (ephemeral_err == 0) {
      ephemeral_err = uv_tcp_init(deps->loop, deps->listener);
      if (ephemeral_err == 0) {
        deps->listener_initialized = true;
        deps->listener->data = deps;
        ephemeral_err = uv_tcp_bind(deps->listener,
                                    (const struct sockaddr *)&ephemeral_addr6,
                                    UV_TCP_IPV6ONLY);
        if (ephemeral_err == 0) {
          ephemeral_err = uv_tcp_nodelay(deps->listener, 1);
        }
        if (ephemeral_err == 0) {
          ephemeral_err = uv_listen((uv_stream_t *)deps->listener, 10,
                                    on_new_connection);
        }
      }
    }
    if (ephemeral_err == 0) {
      logmsg("Listening on port %u.\n", selected_port);
      return true;
    }
    close_listener_handle(deps->listener, &deps->listener_initialized);
    logmsg("IPv6 listener unavailable on ephemeral port %u (%s); "
           "continuing with IPv4 only.\n", selected_port,
           uv_strerror(ephemeral_err));
    return true;
  }

  /* Bind IPv6-only plus IPv4 explicitly. This preserves localhost IPv4 on
   * platforms whose IPv6 wildcard sockets are v6-only, while still allowing
   * an IPv4-only fallback when IPv6 is unavailable. */
  struct sockaddr_in6 addr6;
  int err = uv_ip6_addr("::", (int)port, &addr6);
  if (err == 0) {
    err = uv_tcp_init(deps->loop, deps->listener);
    if (err == 0) {
      deps->listener_initialized = true;
      deps->listener->data = deps;
      err = uv_tcp_bind(deps->listener, (const struct sockaddr *)&addr6,
                        UV_TCP_IPV6ONLY);
      if (err == 0) {
        err = uv_tcp_nodelay(deps->listener, 1);
      }
      if (err == 0) {
        uint16_t selected_port = (uint16_t)port;
        if (port == 0) {
          if (!get_listener_port(deps->listener, AF_INET6, &selected_port)) {
            err = UV_EINVAL;
          }
        }
        if (err == 0) {
          err = uv_listen((uv_stream_t *)deps->listener, 10,
                          on_new_connection);
        }
        if (err == 0 && deps->listener_ipv4) {
          if (!init_ipv4_listener(deps, deps->listener_ipv4,
                                  &deps->listener_ipv4_initialized,
                                  selected_port)) {
            close_listener_handle(deps->listener_ipv4,
                                  &deps->listener_ipv4_initialized);
            close_listener_handle(deps->listener,
                                  &deps->listener_initialized);
            return false;
          }
        }
        if (err == 0) {
          logmsg("Listening on port %u.\n", selected_port);
          return true;
        }
      }
      if (err == UV_EADDRINUSE) {
        logerr("Failed to listen on port %u: %s\n", port,
               uv_strerror(err));
        close_listener_handle(deps->listener, &deps->listener_initialized);
        return false;
      }
      close_listener_handle(deps->listener, &deps->listener_initialized);
    }
  }

  if (!deps->listener_ipv4) {
    logerr("IPv6 listener unavailable and no IPv4 fallback is configured.\n");
    return false;
  }
  if (!init_ipv4_listener(deps, deps->listener_ipv4,
                          &deps->listener_ipv4_initialized, (uint16_t)port)) {
    close_listener_handle(deps->listener_ipv4, &deps->listener_ipv4_initialized);
    return false;
  }
  logmsg("Listening on IPv4 port %u.\n", port);
  return true;
}

void input_processor(uv_timer_t *handle) {
  // The timer callback runs on the loop thread, so input execution remains
  // serialized with network callbacks and task timers.
  RuntimeContext *input_ctx = handle ? (RuntimeContext *)handle->data : NULL;
  if (!input_ctx) {
    logerr("Input runtime context is missing! Cannot continue.\n");
    exit(EXIT_FAILURE);
  }
  if (input_ctx->interrupt_pending && *input_ctx->interrupt_pending) {
    *input_ctx->interrupt_pending = 0;
    if (input_ctx->signal_shutdown_requested) {
      *input_ctx->signal_shutdown_requested = true;
    }
    if (input_ctx->safe_shutdown) *input_ctx->safe_shutdown = false;
    if (input_ctx->shutdown_requested) *input_ctx->shutdown_requested = true;
    logerr("SIGUSR1 received during runtime; shutting down.\n");
    if (input_ctx->loop) uv_stop(input_ctx->loop);
    return;
  }
  ITEM_t *input = find_item(itemstore_root(input_ctx->itemstore), input_ctx->input_name);
  if (!input) {
    logerr("Input item does not exist!  Cannot continue.\n");
    if (input_ctx->safe_shutdown) *input_ctx->safe_shutdown = false;
    if (input_ctx->loop) uv_stop(input_ctx->loop);
    return;
  }
  VALUE_t result = interpret(input_ctx, input);
  value_free(&result);
  if (input_ctx->interrupted) {
    if (input_ctx->signal_shutdown_requested) {
      *input_ctx->signal_shutdown_requested = true;
    }
    if (input_ctx->safe_shutdown) *input_ctx->safe_shutdown = false;
    if (input_ctx->shutdown_requested) *input_ctx->shutdown_requested = true;
    logerr("SIGUSR1 received during runtime; shutting down.\n");
    if (input_ctx->loop) uv_stop(input_ctx->loop);
    return;
  }
  reset_stack(input_ctx->vm->stack);
  // Flush the output of every connected line
  size_t maxconns = input_ctx->maxconns ? *input_ctx->maxconns : input_ctx->network.maxconns ? *input_ctx->network.maxconns : 0;
  for (size_t l = 0; l < maxconns; l++) {
    if (line[l].status != LINE_empty 
                                  && line[l].status != LINE_disconnecting) {
      flush_output(&line[l]);
    }
  }
}

void shutdown_listener_with_deps(NetworkRuntimeDeps *deps) {
  if (!deps) return;
  close_listener_handle(deps->listener, &deps->listener_initialized);
  close_listener_handle(deps->listener_ipv4, &deps->listener_ipv4_initialized);
}

void shutdown_networking(void) {
  // Having been set-up, now shut it down.  Shut it down forever.
  size_t maxconns = network_maxconns ? network_maxconns : config.maxconns;
  for (size_t l = 0; line && l < maxconns; l++) {
    if (line[l].status != LINE_empty || line[l].line_handle ||
        line[l].telnet || line[l].outbuf || line[l].inbuf) {
      destroy_line(&line[l]);
    }
  }
  free(line);
  line = NULL;
  network_maxconns = 0;
}
