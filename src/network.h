// Network interface

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <uv.h>

bool init_listener(uint32_t port);
void shutdown_listener();
void test_callback(uv_timer_t *req);

