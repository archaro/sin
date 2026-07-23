// Task management

// Licensed under the MIT License - see LICENSE file for details.

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "memory.h"
#include "log.h"
#include "task.h"


static uint64_t next_taskid;
static size_t top_of_id_stack;
static size_t capacity_of_id_stack;
static uint64_t *id_stack;

struct TaskNode {
  TASK_t *data;
  struct TaskNode *next;
};

TASKNODE_t *task_list;
static size_t closing_task_count;

void retire_task_id(uint64_t id);

static void unlink_task(TASK_t *task) {
  TASKNODE_t **link;

  if (!task || !task->node) return;
  link = &task_list;
  while (*link && *link != task->node) link = &(*link)->next;
  if (*link == task->node) *link = task->node->next;
}

static void retire_task(TASK_t *task) {
  if (!task) return;
  unlink_task(task);
  retire_task_id(task->id);
}

static void free_task(TASK_t *task) {
  TASKNODE_t *node;

  if (!task) return;
  node = task->node;
  task->node = NULL;
  task->state = TASK_DESTROYED;
  runtime_destroy(&task->runtime_context);
  destroy_vm(task->vm);
  free(task->timer);
  free(node);
  free(task);
}

static void task_on_close(uv_handle_t *handle) {
  TASK_t *task = handle ? (TASK_t *)handle->data : NULL;

  if (!task) return;
  if (closing_task_count > 0) closing_task_count--;
  free_task(task);
}

void init_tasks(void) {
  // Do that which must be done before it is possible to create tasks.
  task_list = NULL;
  next_taskid = 1;
  top_of_id_stack = 0;
  closing_task_count = 0;
  capacity_of_id_stack = 256;
  id_stack = malloc(sizeof *id_stack * capacity_of_id_stack);
  if (!id_stack) capacity_of_id_stack = 0;
}

void finalise_tasks(uv_loop_t *loop) {
  // Remove tasks from lookup immediately, then drain their close callbacks.
  while (task_list) {
    request_task_close(task_list->data);
  }
  if (!loop && closing_task_count > 0) {
    logerr("Cannot finalise active tasks without their event loop.\n");
    return;
  }
  while (closing_task_count > 0) {
    (void)uv_run(loop, UV_RUN_NOWAIT);
  }
  free(id_stack);
  id_stack = NULL;
  top_of_id_stack = 0;
  capacity_of_id_stack = 0;
}

uint64_t new_task_id(void) {
  if (top_of_id_stack == 0) {
    // No retired task ids - grab a new one
    return next_taskid++;
  }
  return id_stack[--top_of_id_stack];
}

void retire_task_id(uint64_t id) {
  // Finished with this task id.  Return it to the stack for the next one.
  if (id == next_taskid - 1) {
    next_taskid--; // Shortcut
    return;
  }
  if (top_of_id_stack == capacity_of_id_stack) {
    size_t new_capacity = capacity_of_id_stack;
    if (!alloc_grow_array_capacity((void **)&id_stack, &new_capacity,
                                   top_of_id_stack + 1u, sizeof *id_stack)) {
      logerr("Unable to retire task id: id stack allocation failed.\n");
      return;
    }
    capacity_of_id_stack = new_capacity;
  }
  id_stack[top_of_id_stack++] = id;
}

TASK_t *make_task(char *itemname, uint64_t interval) {
  // Create a new task
  if (!itemname || strlen(itemname) >= MAX_ITEM_NAME) return NULL;
  TASK_t *task = malloc(sizeof *task);
  if (!task) return NULL;
  memset(task, 0, sizeof *task);
  strcpy(task->itemname, itemname);
  task->vm = make_vm();
  if (!task->vm) {
    free(task);
    return NULL;
  }
  memset(&task->runtime_context, 0, sizeof(task->runtime_context));
  task->runtime_context.vm = task->vm;
  task->interval = interval;
  task->state = TASK_ALLOCATED;
  task->timer = malloc(sizeof *task->timer);
  if (!task->timer) {
    destroy_vm(task->vm);
    free(task);
    return NULL;
  }
  TASKNODE_t *tasknode = malloc(sizeof *tasknode);
  if (!tasknode) {
    free(task->timer);
    destroy_vm(task->vm);
    free(task);
    return NULL;
  }
  task->id = new_task_id();
  tasknode->data = task;
  tasknode->next = task_list;
  task->node = tasknode;
  task_list = tasknode;
  return task;
}

bool start_task_timer(TASK_t *task, uv_loop_t *loop, uv_timer_cb callback,
                      uint64_t start_ms) {
  int timer_rc;

  if (!task || !loop || task->state != TASK_ALLOCATED) {
    return false;
  }
  task->loop = loop;
  timer_rc = uv_timer_init(loop, task->timer);
  if (timer_rc != 0) return false;
  task->state = TASK_INITIALIZED;
  task->timer->data = task;
  timer_rc = uv_timer_start(task->timer, callback, start_ms, task->interval);
  if (timer_rc != 0) {
    request_task_close(task);
    return false;
  }
  task->state = TASK_ACTIVE;
  return true;
}

bool request_task_close(TASK_t *task) {
  bool initialized;

  if (!task || task->state == TASK_CLOSING ||
      task->state == TASK_DESTROYED || !task->node) {
    return false;
  }

  logverbose("Destroying task %" PRIu64 " (%s)\n", task->id, task->itemname);
  retire_task(task);
  if (task->state == TASK_ALLOCATED) {
    free_task(task);
    return true;
  }
  initialized = task->state == TASK_INITIALIZED || task->state == TASK_ACTIVE;
  task->state = TASK_CLOSING;
  if (initialized) uv_timer_stop(task->timer);
  closing_task_count++;
  uv_close((uv_handle_t *)task->timer, task_on_close);
  return true;
}

void destroy_task(TASK_t *task) {
  (void)request_task_close(task);
}

void destroy_task_by_id(uint64_t id) {
  TASK_t *task = find_task_by_id(id);
  if (task) {
    destroy_task(task);
  } else {
    logverbose("Task id %" PRIu64 " not found, and cannot be deleted.\n", id);
  }
}

TASK_t *find_task_by_id(uint64_t id) {
  // Find a task or return null if not found.
  TASKNODE_t *tn = task_list;
  while (tn != NULL) {
    if (tn->data->id == id) {
      return tn->data;
    } else {
      tn = tn->next;
    }
  }
  // Not found
  return NULL;
}

size_t task_list_count(void) {
  size_t count = 0;
  TASKNODE_t *tn = task_list;
  while (tn != NULL) {
    count++;
    tn = tn->next;
  }
  return count;
}
