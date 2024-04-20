// Network interface

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.h"
#include "network.h"
#include "log.h"
#include "util.h"

extern CONFIG_t config;

bool init_listener(uint32_t port) {
  struct addrinfo hints, *res;
  int optval = 1;
  char portstr[12];

  itoa(port, portstr, 10);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  getaddrinfo(NULL, portstr, &hints, &res);

  // make a socket, bind it, and listen on it:
  config.fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  setsockopt(config.fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
  if (config.fd == -1) {
    logerr("Unable to create socket: %s\n", strerror(errno));
    return false;
  }
  if (bind(config.fd, res->ai_addr, res->ai_addrlen) == -1) {
    logerr("Unable to bind socket: %s\n", strerror(errno));
    return false;
  }
  listen(config.fd, 10);
  freeaddrinfo(res);
  logmsg("Listening on port %d (fd %d).\n", port, config.fd);
  return true;
}

void shutdown_listener() {
  // Just a wrapper to close() to encapsulate the API
  logmsg("Stopping listener.\n");
  close(config.fd);
}

void test_callback(uv_timer_t *req) {
  static int count = 0;

  logmsg("(%d) Callback meep!.\n", count);
  count++;
  if (count>2) {
    // We need to call uv_close to remove ourself from the event loop
    uv_close((uv_handle_t*)req, NULL);
    uv_stop(config.loop);
  }
}

