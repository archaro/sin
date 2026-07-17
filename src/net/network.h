// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <uv.h>

#include "libtelnet.h"

// Default maximum connections
#define MAXCONNS  50

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    size_t length; // Allocated size of buffer
} write_req_t;

typedef enum {
  LINE_empty,
  LINE_connecting,
  LINE_disconnecting,
  LINE_data,
  LINE_idle
} LINE_STATUS_t;

typedef struct {
  uv_tcp_t *line_handle;
  LINE_STATUS_t status;
  size_t linenum;
  char address[40];
  telnet_t *telnet;
  write_req_t *outbuf;
  write_req_t *inbuf;
  bool output_write_in_flight;
  size_t output_in_flight_length;
  uint32_t output_backpressure_ticks;
  bool close_after_output;
  size_t input_line_length; // Bytes buffered since the last newline
} LINE_t;

extern LINE_t *line;

typedef struct {
  uv_loop_t *loop;
  uv_tcp_t *listener;
  LINE_t **lines;
  size_t maxconns;
} NetworkRuntimeDeps;

bool validate_network_deps(const NetworkRuntimeDeps *deps);
bool line_is_active(const LINE_t *linep);
bool line_is_disconnect_pending(const LINE_t *linep);
bool line_is_disconnected(const LINE_t *linep);
bool line_is_reusable(const LINE_t *linep);
void init_networking_with_deps(NetworkRuntimeDeps *deps);
void init_listener_with_deps(NetworkRuntimeDeps *deps, uint32_t port);
void client_on_close(uv_handle_t *handle);
void destroy_line(LINE_t *line);
void input_processor(uv_idle_t* handle);
char *get_input(LINE_t *line);
void flush_output(LINE_t *line);
void request_line_disconnect(LINE_t *line);
void shutdown_listener_with_deps(NetworkRuntimeDeps *deps);
void shutdown_networking(void);
bool line_can_accept_output(LINE_t *linep, size_t len);
