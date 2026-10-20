#ifndef DODO_KERNEL_TASK_H
#define DODO_KERNEL_TASK_H

#include <stdint.h>

#define DODO_TASK_CAPACITY 16u
#define DODO_TASK_NAME_MAX 23u
#define DODO_TASK_INVALID_ID UINT32_C(0)

enum dodo_task_state {
    DODO_TASK_UNUSED = 0,
    DODO_TASK_READY,
    DODO_TASK_RUNNING,
    DODO_TASK_BLOCKED,
    DODO_TASK_ZOMBIE
};

enum dodo_task_status {
    DODO_TASK_OK = 0,
    DODO_TASK_INVALID_ARGUMENT,
    DODO_TASK_FULL,
    DODO_TASK_NOT_FOUND,
    DODO_TASK_INVALID_STATE,
    DODO_TASK_NO_RUNNABLE_TASK
};

struct dodo_task {
    uint32_t id;
    enum dodo_task_state state;
    uint64_t saved_stack_pointer;
    uint64_t address_space_root;
    uint32_t exit_code;
    char name[DODO_TASK_NAME_MAX + 1u];
};

struct dodo_scheduler {
    struct dodo_task tasks[DODO_TASK_CAPACITY];
    uint32_t next_id;
    uint32_t current_id;
    uint32_t cursor;
    uint32_t task_count;
};

/* Initialize a fixed-capacity scheduler; no heap or hardware is required. */
void dodo_scheduler_init(struct dodo_scheduler *scheduler);

/* Register a task with caller-provided initial stack and address-space root. */
enum dodo_task_status dodo_scheduler_create(
    struct dodo_scheduler *scheduler,
    const char *name,
    uint64_t saved_stack_pointer,
    uint64_t address_space_root,
    uint32_t *task_id);

/* Select the next READY task in round-robin order, excluding zombies/blocked tasks. */
enum dodo_task_status dodo_scheduler_schedule(
    struct dodo_scheduler *scheduler, uint32_t *task_id);

/* Save a nonzero stack pointer for the currently RUNNING task only. */
enum dodo_task_status dodo_scheduler_save_stack_pointer(
    struct dodo_scheduler *scheduler,
    uint32_t task_id,
    uint64_t saved_stack_pointer);

/* Mark the current task READY, BLOCKED, or ZOMBIE. */
enum dodo_task_status dodo_scheduler_set_current_state(
    struct dodo_scheduler *scheduler,
    enum dodo_task_state state,
    uint32_t exit_code);

/* Wake a blocked task by ID. */
enum dodo_task_status dodo_scheduler_wake(
    struct dodo_scheduler *scheduler, uint32_t task_id);

/* Reclaim a ZOMBIE slot after the owner has handled its exit status. */
enum dodo_task_status dodo_scheduler_reap(
    struct dodo_scheduler *scheduler, uint32_t task_id);

/* Find a task by ID; returns NULL for invalid IDs or absent tasks. */
struct dodo_task *dodo_scheduler_find(
    struct dodo_scheduler *scheduler, uint32_t task_id);

#endif
