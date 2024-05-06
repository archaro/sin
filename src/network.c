// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>

#include "config.h"
#include "network.h"
#include "log.h"
#include "util.h"

extern CONFIG_t config;

#define OUTBUF_LENGTH 16384
#define INBUF_LENGTH 16384

typedef struct LineNode {
  LINE_t *data;
  struct LineNode* next;
} LINENODE_t;

// This is the list of all currently-connected lines
LINENODE_t *line_list = NULL;

LINENODE_t *new_node(LINE_t *line) {
  LINENODE_t *newline = (LINENODE_t *)malloc(sizeof(LINENODE_t));
  newline->data = line;
  newline->next = NULL;
  return newline;
}

LINE_t *add_line(uv_tcp_t *line_handle) {
  LINE_t *line = (LINE_t *)malloc(sizeof(LINE_t));
  line->line_handle = line_handle;
  line->outbuf = (write_req_t *) malloc(sizeof(write_req_t));
  line->outbuf->buf.len = OUTBUF_LENGTH;
  line->outbuf->buf.base = (char *)calloc(OUTBUF_LENGTH, 1);
  line->outbuf->buf.base[0] = '\0';
  line->outbuf->length = 0;
  line->inbuf = (write_req_t *) malloc(sizeof(write_req_t));
  line->inbuf->buf.len = INBUF_LENGTH;
  line->inbuf->buf.base = (char *)calloc(INBUF_LENGTH, 1);
  line->inbuf->buf.base[0] = '\0';
  line->inbuf->length = 0;
  LINENODE_t *newnode = new_node(line);
  newnode->next = line_list;
  line_list = newnode;
  return line;
}

void destroy_line(LINE_t *line) {
  // Clean up the line object
  logmsg("%s disconnected.\n", line->address);
  free(line->line_handle);
  free(line->outbuf->buf.base);
  free(line->outbuf);
  free(line->inbuf->buf.base);
  free(line->inbuf);
  free(line);
}

void remove_line(uv_tcp_t *line_handle) {
  // Given a line handle, remove it from the list and free it
  // (the line MUST have been closed first!)
  LINENODE_t *temp = line_list, *prev = NULL;
  if (temp != NULL && temp->data->line_handle == line_handle) {
    // Special case for the first line in the list
    line_list = temp->next;
    destroy_line(temp->data);
    free(temp);
    return;
  }
  while (temp != NULL && temp->data->line_handle != line_handle) {
    prev = temp;
    temp = temp->next;
  }
  if (temp == NULL) {
    // Not found
    return;
  }
  prev->next = temp->next;
  destroy_line(temp->data);
  free(temp);
}

LINE_t *find_line(uv_tcp_t *client) {
  // Given a TCP client connection, find its associated line.
  // Return the line, or NULL if not found.
  LINENODE_t *temp = line_list;
  while (temp) {
    if (temp->data->line_handle == client) {
      return temp->data;
    } else {
      temp = temp->next;
    }
  }
  // Not found!
  return NULL;
}

void network_cleanup() {
  // Cleanup after shutdown
  LINENODE_t *temp = line_list;
  while (line_list != NULL) {
    temp = line_list;
    line_list = line_list->next;
    destroy_line(temp->data);
    free(temp);
  }
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
  remove_line((uv_tcp_t *)handle);
}

void client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    logmsg("Data received (%d bytes): %s\n", nread, buf->base);
    LINE_t *line = find_line((uv_tcp_t *)client);
    if (line) {
      append_input(line, buf->base);
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
    append_output(newline, "Connected.\r\n");
    flush_output(newline);
    struct sockaddr_storage peername = {0};
    int peernamelen = sizeof(peername);
    uv_tcp_getpeername(client, (struct sockaddr *)&peername, &peernamelen);
    uv_ip_name((struct sockaddr *)&peername, newline->address, 40);
    uv_read_start((uv_stream_t *)client, alloc_buffer, client_read);
    logmsg("New connection from %s\n", newline->address);
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

void shutdown_listener() {
  uv_close((uv_handle_t *)&config.listener, NULL);
}

