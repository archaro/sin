#ifndef TEST_NETWORK_FIXTURE_H
#define TEST_NETWORK_FIXTURE_H

#include <stddef.h>

#include "network.h"

typedef enum {
  NETWORK_TEST_EMPTY = 0,
  NETWORK_TEST_CONNECTING = 1,
  NETWORK_TEST_DISCONNECTING = 2,
  NETWORK_TEST_DATA = 3,
  NETWORK_TEST_IDLE = 4,
  NETWORK_TEST_IDLE_NO_TRANSPORT = 5
} NetworkTestLineState;

bool network_runtime_test_set_line(NetworkRuntime *runtime, size_t line_index,
                                   int state);
bool network_runtime_test_set_input(NetworkRuntime *runtime, size_t line_index,
                                    const char *input);
bool network_runtime_test_feed(NetworkRuntime *runtime, size_t line_index,
                               const char *data, size_t length);
bool network_runtime_test_set_address(NetworkRuntime *runtime,
                                      size_t line_index, const char *address);
bool network_runtime_test_select_line(NetworkRuntime *runtime,
                                      size_t line_index);
size_t network_runtime_test_take_output(NetworkRuntime *runtime,
                                        size_t line_index, unsigned char *out,
                                        size_t out_size);

#endif
