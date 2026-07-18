// Task management

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <uv.h>

#include "vm.h"
#include "item.h"
#include "runtime_context.h"

typedef enum {
  TASK_ALLOCATED,
  TASK_INITIALIZED,
  TASK_ACTIVE,
  TASK_CLOSING,
  TASK_DESTROYED
} TASK_STATE_e;

typedef struct TaskNode TASKNODE_t;

typedef struct {
  uint64_t id;
  uint64_t interval; // milliseconds
  uv_timer_t *timer;
  TASKNODE_t *node;
  VM_t *vm;
  ITEM_t *itemroot;
  uv_loop_t *loop;
  RuntimeContext runtime_context;
  TASK_STATE_e state;
  char itemname[MAX_ITEM_NAME];
} TASK_t;

void init_tasks(void);
// Requests all tasks to close and drains close callbacks on loop before freeing
// task ID storage. The loop must own all initialized task timers.
void finalise_tasks(uv_loop_t *loop);
TASK_t *make_task(char *name, uint64_t interval);
bool start_task_timer(TASK_t *task, uv_loop_t *loop, uv_timer_cb callback,
                      uint64_t start_ms);
bool request_task_close(TASK_t *task);
void destroy_task(TASK_t *task);
void destroy_task_by_id(uint64_t id);
TASK_t *find_task_by_id(uint64_t id);
