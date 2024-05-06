// Task management

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <uv.h>

#include "vm.h"
#include "item.h"

typedef struct {
  uint64_t id;
  uint64_t interval; // centiseconds
  uv_timer_t *timer;
  VM_t *vm;
  char itemname[MAX_ITEM_NAME];
} TASK_t;

void init_tasks();
void finalise_tasks();
TASK_t *make_task(char *name, uint64_t interval);
void destroy_task(TASK_t *task);
void destroy_task_by_id(uint64_t id);
TASK_t *find_task_by_id(uint64_t id);

