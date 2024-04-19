// Network interface

#pragma once

#include <stdint.h>
#include <stdbool.h>

bool init_listener(uint32_t port);
void shutdown_listener();

