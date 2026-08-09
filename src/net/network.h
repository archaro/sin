// Network interface

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <uv.h>

/* Default maximum connections. */
#define MAXCONNS 50

typedef struct NetworkRuntime NetworkRuntime;

typedef enum {
  NETWORK_EVENT_NONE = 0,
  NETWORK_EVENT_CONNECT = 1,
  NETWORK_EVENT_DISCONNECT = 2,
  NETWORK_EVENT_DATA = 3
} NetworkEvent;

typedef enum {
  NETWORK_WRITE_INACTIVE = 0,
  NETWORK_WRITE_SENT = 1,
  NETWORK_WRITE_REJECTED = 2
} NetworkWriteResult;

/*
 * A NetworkRuntime owns all connection slots and their transport state.  The
 * loop and listener storage are borrowed from the caller: they must remain
 * alive until network_runtime_destroy() has been called after the loop has
 * drained all close callbacks. network_runtime_create() owns its allocation
 * and slot storage; destroy releases both after shutdown is complete and
 * returns false without mutating the runtime if live transport state remains.
 */
NetworkRuntime *network_runtime_create(uv_loop_t *loop, uv_tcp_t *listener,
                                       uv_tcp_t *listener_ipv4,
                                       size_t maxconns);
bool network_runtime_listen(NetworkRuntime *runtime, uint32_t port);
void network_runtime_shutdown(NetworkRuntime *runtime);
bool network_runtime_destroy(NetworkRuntime *runtime);
size_t network_runtime_max_connections(const NetworkRuntime *runtime);
size_t network_runtime_current_line(const NetworkRuntime *runtime);
NetworkEvent network_runtime_poll(NetworkRuntime *runtime, size_t *line_index,
                                  char **input);
NetworkWriteResult network_runtime_write(NetworkRuntime *runtime,
                                         size_t line_index, const char *text,
                                         size_t length);
/* Flush and disconnect accept active connecting, idle, or data lines. */
bool network_runtime_flush(NetworkRuntime *runtime, size_t line_index);
void network_runtime_flush_all(NetworkRuntime *runtime);
/* Echo negotiation is limited to writable idle or data Telnet lines. */
void network_runtime_echo(NetworkRuntime *runtime, size_t line_index,
                          bool enabled);
bool network_runtime_disconnect(NetworkRuntime *runtime, size_t line_index);
bool network_runtime_connected(const NetworkRuntime *runtime,
                              size_t line_index);
char *network_runtime_address(const NetworkRuntime *runtime,
                              size_t line_index);

/* Walk integration used by process shutdown to drain all libuv handles. */
void close_network_handle(NetworkRuntime *runtime, uv_handle_t *handle);
void network_close_walk_cb(uv_handle_t *handle, void *arg);
void input_processor(uv_timer_t *handle);
