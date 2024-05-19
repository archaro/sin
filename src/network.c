// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>
#include <unistd.h>

#include "config.h"
#include "memory.h"
#include "network.h"
#include "log.h"
#include "util.h"
#include "interpret.h"

// Some shorthand
#define VM config.vm

extern CONFIG_t config;

#define OUTBUF_LENGTH 16384
#define INBUF_LENGTH 16384

static const telnet_telopt_t telopts[] = {
  { TELNET_TELOPT_ECHO, TELNET_WONT, TELNET_DO },
	{ -1, 0, 0 }
};

LINE_t *line;

void init_networking() {
  // Do that which needs to be done before starting the network interface

  line = GROW_ARRAY(LINE_t, line, 0, config.maxconns);
  for (int l = 0; l < config.maxconns; l++) {
    line[l].status = LINE_empty;
    line[l].linenum = l;
  }
}

LINE_t *add_line(uv_tcp_t *line_handle) {
  uint8_t l = 0;
  while (line[l].status != LINE_empty) {
    l++;
    if (l >= config.maxconns) {
      return NULL;
    }
  }
  line[l].line_handle = line_handle;
  line[l].status = LINE_connecting;
  line[l].outbuf = (write_req_t *) malloc(sizeof(write_req_t));
  line[l].outbuf->buf.len = 0;
  line[l].outbuf->buf.base = (char *)calloc(OUTBUF_LENGTH, 1);
  line[l].outbuf->buf.base[0] = '\0';
  line[l].outbuf->length = OUTBUF_LENGTH;
  line[l].inbuf = (write_req_t *) malloc(sizeof(write_req_t));
  line[l].inbuf->buf.len = 0;
  line[l].inbuf->buf.base = (char *)calloc(INBUF_LENGTH, 1);
  line[l].inbuf->buf.base[0] = '\0';
  line[l].inbuf->length = INBUF_LENGTH;
  return &line[l];
}

void destroy_line(LINE_t *line) {
  // Clean up the line object
  telnet_free(line->telnet);
  free(line->line_handle);
  free(line->outbuf->buf.base);
  free(line->outbuf);
  free(line->inbuf->buf.base);
  free(line->inbuf);
  line->status = LINE_empty;
}

LINE_t *find_line(uv_tcp_t *client) {
  // Given a TCP client connection, find its associated line.
  // Return the line, or NULL if not found.
  uint8_t l = 0;
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
  LINE_t *line = find_line((uv_tcp_t *)handle);
  logmsg("Line %d: %s disconnected.\n", line->linenum, line->address);
  line->status = LINE_disconnecting;
}

void append_output(LINE_t *line, const char *msg, const ssize_t len) {
  // Append output to the buffer for this line, ready for sending later.
  // If the buffer is too small, embiggen it.
  if (line->status != LINE_empty && line->status != LINE_disconnecting) {
  // No point in doing this if there is no connection.
    while (line->outbuf->buf.len + len + 1 >= line->outbuf->length) {
      line->outbuf->length += OUTBUF_LENGTH;
      line->outbuf->buf.base = (char *)realloc(line->outbuf->buf.base,
                                                      line->outbuf->length);
    }
    memcpy(line->outbuf->buf.base + line->outbuf->buf.len, msg, len);
    line->outbuf->buf.len += len;
  }
}

void append_input(LINE_t *line, const char *msg, const ssize_t len) {
  // Append input to the input buffer, ready for processing later.
  // Embiggen the buffer if too small.
  // This is where telnet processing will happen.
  if (line->status != LINE_empty && line->status != LINE_disconnecting) {
    // No point in doing this if there is no connection.
    // If there is a newline in the input, we have received
    // a complete line of input.
    if (memchr(msg, '\n', len)) {
      line->status = LINE_data;
    }
    while (line->inbuf->buf.len + len + 1 >= line->inbuf->length) {
      line->inbuf->length += INBUF_LENGTH;
      line->inbuf->buf.base = (char *)realloc(line->inbuf->buf.base,
                                                    line->inbuf->length);
    }
    memcpy(line->inbuf->buf.base + line->inbuf->buf.len, msg, len);
    line->inbuf->buf.len += len;
    line->inbuf->buf.base[line->inbuf->buf.len] = '\0';
  }
}

char *get_input(LINE_t *line) {
  // Extract a line of input from the input buffer.  Should only be called
  // when the line status is LINE_data.  If there is nothing left in the
  // input buffer, set the status to LINE_idle, otherwise leave it
  // unchanged.  The buffer allocated by this function will need to be
  // freed by the calling function when it is no longer needed.
  // If there isn't a newline in the input buffer, explode messily.
  char *eol = strchr(line->inbuf->buf.base, '\n');
  *eol = '\0';
  char *data = strdup(line->inbuf->buf.base);
  // Ok, we have the line of data, now take it out of the input buffer.
  eol++;
  char *newbuffer = malloc(INBUF_LENGTH);
  strcpy(newbuffer, eol);
  free(line->inbuf->buf.base);
  line->inbuf->length = INBUF_LENGTH;
  line->inbuf->buf.base = newbuffer;
  line->inbuf->buf.len = strlen(newbuffer);
  if (!strchr(line->inbuf->buf.base, '\n')) {
    line->status = LINE_idle;
  }
  return data;
}

void telnet_event_handler(telnet_t *telnet, telnet_event_t *ev,
                                                    		void *user_data) {
	LINE_t *line = (LINE_t *)user_data;
	switch (ev->type) {
	case TELNET_EV_DATA:
	  // Data received from client - process it.
    append_input(line, ev->data.buffer, ev->data.size);
		break;
	case TELNET_EV_SEND:
	  // Data to be sent to client - process it, too.
    append_output(line, ev->data.buffer, ev->data.size);
		break;
	case TELNET_EV_DO:
    // Here is where we negotiate requests to do something
		break;
	case TELNET_EV_ERROR:
    // If there is a telnet error, it is essentially impossible to recover.
    logerr("Telnet negotiation error.\n");
    uv_close((uv_handle_t *)line->line_handle, client_on_close);
		break;
	default:
		// I don't know you
		break;
	}
}

void flush_output(LINE_t *line) {
  // Send the output to the line, and reset the buffer.
  if (line->outbuf->buf.len > 0) {
    uv_write((uv_write_t*) &line->outbuf->req,
            (uv_stream_t*)line->line_handle, &line->outbuf->buf, 1, NULL);
    line->outbuf->buf.len = 0;
    line->outbuf->buf.base[0] = '\0';
  }
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  // Buffer for input from connected client
  buf->base = (char*)calloc(suggested_size, 1);
  buf->len = suggested_size;
}

void client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    LINE_t *line = find_line((uv_tcp_t *)client);
    if (line) {
      telnet_recv(line->telnet, buf->base, nread);
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
  if (status < 0) {
    logerr("Error on new connection: %s\n", uv_strerror(status));
    return;
  }
  uv_tcp_t *client = (uv_tcp_t *) malloc(sizeof(uv_tcp_t));
  uv_tcp_init(config.loop, client);
  if (uv_accept((uv_stream_t *)&config.listener,
                                              (uv_stream_t *)client) == 0) {
    LINE_t *newline = add_line(client);
    if (!newline) {
      // We have no line setup here, so this has to be done differently
      uv_buf_t gamefull = {"Too many connections.\r\n", 23};
      uv_try_write((uv_stream_t *)client, &gamefull, 1);
      logmsg("Maximum connections (%d) exceeded.\n", config.maxconns);
      uv_close((uv_handle_t *)client, client_on_close);
    }
    newline->telnet = telnet_init(telopts, telnet_event_handler, 
                                              TELNET_FLAG_NVT_EOL, newline);
    telnet_printf(newline->telnet, "Connected.\n");
    flush_output(newline);
    struct sockaddr_storage peername = {0};
    int peernamelen = sizeof(peername);
    uv_tcp_getpeername(client, (struct sockaddr *)&peername, &peernamelen);
    uv_ip_name((struct sockaddr *)&peername, newline->address, 40);
    uv_read_start((uv_stream_t *)client, alloc_buffer, client_read);
    logmsg("Line %d: %s connected.\n", newline->linenum,
                                                          newline->address);
  }
  else {
    uv_close((uv_handle_t *)client, client_on_close);
  }
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
    logmsg("Listening on port %d.\n", port);
  }
}

void input_processor(uv_idle_t* handle) {
  // Called once per iteration of the game loop
  config.vm = config.input_vm;
  ITEM_t *input = find_item(config.itemroot, config.input);
  if (!input) {
    logerr("Input item does not exist!  Cannot continue.\n");
    exit(EXIT_FAILURE);
  }
  interpret(input);
  reset_stack(VM->stack);
  // Flush the output of every connected line
  for (int l = 0; l < config.maxconns; l++) {
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
  FREE_ARRAY(LINE_t, line, config.maxconns);
}

