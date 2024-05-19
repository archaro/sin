// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <sys/socket.h>
#include <uv.h>

#include "libtelnet.h"

// Default maximum connections
#define MAXCONNS  50

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    int length; // Allocated size of buffer
} write_req_t;

typedef struct {
  uv_tcp_t *line_handle;
  enum { LINE_empty, LINE_connecting,
         LINE_disconnecting, LINE_data, LINE_idle } status;
  uint8_t linenum;
  char address[40];
  telnet_t *telnet;
  write_req_t *outbuf;
  write_req_t *inbuf;
} LINE_t;

void init_networking();
void init_listener(uint32_t port);
void destroy_line(LINE_t *line);
void input_processor(uv_idle_t* handle);
char *get_input(LINE_t *line);
void shutdown_listener();
void shutdown_networking();

