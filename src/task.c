// Task management

// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>

#include "config.h"
#include "memory.h"
#include "log.h"
#include "task.h"

extern CONFIG_t config;

static uint64_t next_taskid;
static size_t top_of_id_stack;
static size_t capacity_of_id_stack;
static uint64_t *id_stack;

typedef struct TaskNode {
  TASK_t *data;
  struct TaskNode* next;
} TASKNODE_t;

TASKNODE_t *task_list;

void init_tasks() {
  // Do that which must be done before it is possible to create tasks.
  task_list = NULL;
  next_taskid = 1;
  top_of_id_stack = 0;
  capacity_of_id_stack = 256;
  id_stack = GROW_ARRAY(uint64_t, id_stack, 0, capacity_of_id_stack);
}

void finalise_tasks() {
  // Called before shutdown to clean up memory.
  FREE_ARRAY(uint64_t, id_stack, capacity_of_id_stack);
  TASKNODE_t *temp;
  while (task_list) {
    temp = task_list;
    task_list = temp->next;
    destroy_task(temp->data);
    FREE_ARRAY(TASKNODE_t, temp, 1);
  }
}

uint64_t new_task_id() {
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
    capacity_of_id_stack *= 2;
    id_stack = GROW_ARRAY(uint64_t, id_stack, capacity_of_id_stack / 2,
                                                      capacity_of_id_stack);
  }
  id_stack[top_of_id_stack++] = id;
}

TASK_t *make_task(char *itemname, uint64_t interval) {
  // Create a new task
  TASK_t *task = NULL;
  task = GROW_ARRAY(TASK_t, task, 0, 1);
  strcpy(task->itemname, itemname);
  task->vm = make_vm();
  task->id = new_task_id();
  task->interval = interval;
  task->timer = GROW_ARRAY(uv_timer_t, task->timer, 0, 1);
  TASKNODE_t *tasknode = NULL;
  tasknode = GROW_ARRAY(TASKNODE_t, tasknode, 0, 1);
  tasknode->data = task;
  tasknode->next = task_list;
  task_list = tasknode;
  return task;
}

void destroy_task(TASK_t *task) {
  // Assumes task definitely exists in the task list!
  logmsg("Destroying task %d (%s)\n", task->id, task->itemname);
  destroy_vm(task->vm);
  FREE_ARRAY(uv_timer_t, task->timer, 1);
  retire_task_id(task->id);
  FREE_ARRAY(TASK_t, task, 1);
  TASKNODE_t *temp = task_list, *prev = NULL;
  if (temp != NULL && temp->data == task) {
    // Special case for the first task in the list
    task_list = temp->next;
    free(temp);
    return;
  }
  while (temp != NULL && temp->data != task) {
    prev = temp;
    temp = temp->next;
  }
  if (temp) {
    prev->next = temp->next;
    free(temp);
  }
}

void destroy_task_by_id(uint64_t id) {
  TASK_t *task = find_task_by_id(id);
  if (task) {
    destroy_task(task);
  } else {
    logmsg("Task id %d not found, and cannot be deleted.\n", id);
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

