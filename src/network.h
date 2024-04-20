// Network interface

#pragma once

#include <stdint.h>
#include <sys/socket.h>
#include <uv.h>

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    int length;
} write_req_t;

typedef struct {
  uv_tcp_t *line_handle;
  char address[40];
  write_req_t *outbuf;
  write_req_t *inbuf;
} LINE_t;

void network_cleanup();
void init_listener(uint32_t port);
void shutdown_listener();
void test_callback(uv_timer_t *req);

