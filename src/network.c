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
  line[l].outbuf->buf.len = OUTBUF_LENGTH;
  line[l].outbuf->buf.base = (char *)calloc(OUTBUF_LENGTH, 1);
  line[l].outbuf->buf.base[0] = '\0';
  line[l].outbuf->length = 0;
  line[l].inbuf = (write_req_t *) malloc(sizeof(write_req_t));
  line[l].inbuf->buf.len = INBUF_LENGTH;
  line[l].inbuf->buf.base = (char *)calloc(INBUF_LENGTH, 1);
  line[l].inbuf->buf.base[0] = '\0';
  line[l].inbuf->length = 0;
  return &line[l];
}

void destroy_line(LINE_t *line) {
  // Clean up the line object
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

void append_output(LINE_t *line, char *msg) {
  // Append output to the buffer for this line, ready for sending later.
  // If the buffer is too small, embiggen it.
  int msglen = strlen(msg);
  while (line->outbuf->length + msglen + 1 >= line->outbuf->buf.len) {
    line->outbuf->buf.len += OUTBUF_LENGTH;
    line->outbuf->buf.base = (char *)realloc(line->outbuf->buf.base,
                                                    line->outbuf->buf.len);
  }
  strcat(line->outbuf->buf.base, msg);
  line->outbuf->length += msglen;
}

void append_input(LINE_t *line, char *msg) {
  // Append input to the input buffer, ready for processing later.
  // Embiggen the buffer if too small.
  // This is where telnet processing will happen.
  int msglen = strlen(msg);
  while (line->inbuf->length + msglen + 1 >= line->inbuf->buf.len) {
    line->inbuf->buf.len += INBUF_LENGTH;
    line->inbuf->buf.base = (char *)realloc(line->inbuf->buf.base,
                                                    line->inbuf->buf.len);
  }
  strcat(line->inbuf->buf.base, msg);
  line->inbuf->length += msglen;
  logmsg("Input buffer now contains: >>>%s<<<\n", line->inbuf->buf.base);
}

void flush_output(LINE_t *line) {
  // Send the output to the line, and reset the buffer.
  uv_write((uv_write_t*) &line->outbuf->req,
              (uv_stream_t*)line->line_handle, &line->outbuf->buf, 1, NULL);
  line->outbuf->length = 0;
  line->outbuf->buf.base[0] = '\0';
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  // Buffer for input from connected client
  buf->base = (char*)calloc(suggested_size, 1);
  buf->len = suggested_size;
}

void client_on_close(uv_handle_t *handle) {
  LINE_t *line = find_line((uv_tcp_t *)handle);
  logmsg("%s disconnected.\n", line->address);
  line->status = LINE_disconnecting;
  // Don't call destroy_line here or it will overwrite the status
  // Needs to be called by the input processor after dealing with
  // the disconnection.
  //destroy_line(line);
}

void client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    logmsg("Data received (%d bytes): %s\n", nread, buf->base);
    LINE_t *line = find_line((uv_tcp_t *)client);
    if (line) {
      line->status = LINE_data;
      append_input(line, buf->base);
    }
    free(buf->base);
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF)
      logerr("Read error %s\n", uv_err_name(nread));
    line->status = LINE_disconnecting;
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
      append_output(newline, "Too many connections.\r\n");
      flush_output(newline);
      logmsg("Maximum connections (%d) exceeded.\n", config.maxconns);
      uv_close((uv_handle_t *)client, client_on_close);
    }
    append_output(newline, "Connected.\r\n");
    flush_output(newline);
    struct sockaddr_storage peername = {0};
    int peernamelen = sizeof(peername);
    uv_tcp_getpeername(client, (struct sockaddr *)&peername, &peernamelen);
    uv_ip_name((struct sockaddr *)&peername, newline->address, 40);
    uv_read_start((uv_stream_t *)client, alloc_buffer, client_read);
    logmsg("New connection from %s\n", newline->address);
    line->status = LINE_connecting;
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
  interpret(input);
  reset_stack(VM->stack);
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

