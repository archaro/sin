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
    line[l].address[0] = '\0';
    line[l].line_handle = NULL;
    line[l].telnet = NULL;
    line[l].outbuf = NULL;
    line[l].inbuf = NULL;
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
  linep->status = LINE_empty;
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
  LINE_t *linep = find_line((uv_tcp_t *)handle);
  if (!linep) {
    free(handle);
    return;
  }
  logmsg("Line %d: %s disconnected.\n", linep->linenum, linep->address);
  linep->status = LINE_disconnecting;
}

void append_output(LINE_t *linep, const char *msg, const ssize_t len) {
  // Append output to the buffer for this line, ready for sending later.
  // If the buffer is too small, embiggen it.
  if (len <= 0) return;
  size_t msg_len = (size_t)len;
  if (linep->status != LINE_empty && linep->status != LINE_disconnecting) {
  // No point in doing this if there is no connection.
    while (linep->outbuf->buf.len + msg_len + 1 >= linep->outbuf->length) {
      linep->outbuf->length += OUTBUF_LENGTH;
      linep->outbuf->buf.base = (char *)realloc(linep->outbuf->buf.base,
                                                      linep->outbuf->length);
    }
    memcpy(linep->outbuf->buf.base + linep->outbuf->buf.len, msg, msg_len);
    linep->outbuf->buf.len += msg_len;
  }
}

void append_input(LINE_t *linep, const char *msg, const ssize_t len) {
  // Append input to the input buffer, ready for processing later.
  // Embiggen the buffer if too small.
  // This is where telnet processing will happen.
  if (len <= 0) return;
  size_t msg_len = (size_t)len;
  if (linep->status != LINE_empty && linep->status != LINE_disconnecting) {
    // No point in doing this if there is no connection.
    // If there is a newline in the input, we have received
    // a complete line of input.
    if (memchr(msg, '\n', msg_len)) {
      linep->status = LINE_data;
    }
    while (linep->inbuf->buf.len + msg_len + 1 >= linep->inbuf->length) {
      linep->inbuf->length += INBUF_LENGTH;
      linep->inbuf->buf.base = (char *)realloc(linep->inbuf->buf.base,
                                                    linep->inbuf->length);
    }
    memcpy(linep->inbuf->buf.base + linep->inbuf->buf.len, msg, msg_len);
    linep->inbuf->buf.len += msg_len;
    linep->inbuf->buf.base[linep->inbuf->buf.len] = '\0';
  }
}

char *get_input(LINE_t *linep) {
  // Extract a line of input from the input buffer.  Should only be called
  // when the line status is LINE_data.  If there is nothing left in the
  // input buffer, set the status to LINE_idle, otherwise leave it
  // unchanged.  The buffer allocated by this function will need to be
  // freed by the calling function when it is no longer needed.
  // If there isn't a newline in the input buffer, explode messily.
  char *eol = strchr(linep->inbuf->buf.base, '\n');
  *eol = '\0';
  char *data = strdup(linep->inbuf->buf.base);
  if (!data) {
    logerr("Failed to allocate input line for line %d.\n", linep->linenum);
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return NULL;
  }
  // Ok, we have the line of data, now take it out of the input buffer.
  eol++;
  char *newbuffer = malloc(INBUF_LENGTH);
  if (!newbuffer) {
    logerr("Failed to allocate replacement input buffer for line %d.\n",
           linep->linenum);
    free(data);
    linep->status = LINE_disconnecting;
    if (linep->line_handle && !uv_is_closing((uv_handle_t *)linep->line_handle)) {
      uv_close((uv_handle_t *)linep->line_handle, client_on_close);
    }
    return NULL;
  }
  strcpy(newbuffer, eol);
  free(linep->inbuf->buf.base);
  linep->inbuf->length = INBUF_LENGTH;
  linep->inbuf->buf.base = newbuffer;
  linep->inbuf->buf.len = strlen(newbuffer);
  if (!strchr(linep->inbuf->buf.base, '\n')) {
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

void flush_output(LINE_t *linep) {
  // Send the output to the line, and reset the buffer.
  if (linep->outbuf->buf.len > 0) {
    uv_write((uv_write_t*) &linep->outbuf->req,
            (uv_stream_t*)linep->line_handle, &linep->outbuf->buf, 1, NULL);
    linep->outbuf->buf.len = 0;
    linep->outbuf->buf.base[0] = '\0';
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
    logmsg("Rejected connection: maximum connections (%d) exceeded or allocation failed.\n",
           config.maxconns);
    uv_close((uv_handle_t *)client, free_client_on_close);
    return;
  }

  newline->telnet = telnet_init(telopts, telnet_event_handler,
                                TELNET_FLAG_NVT_EOL, newline);
  if (!newline->telnet) {
    logerr("Rejected connection: telnet_init failed for line %d.\n",
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
  logmsg("Line %d: %s connected.\n", newline->linenum, newline->address);
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
  (void)handle;
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
