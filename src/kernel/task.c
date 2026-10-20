#include <stddef.h>
#include <stdint.h>

#include "dodo/kernel/task.h"

static int valid_scheduler(const struct dodo_scheduler *scheduler) {
    return dodo_scheduler_validate(scheduler) == DODO_TASK_OK;
}

void dodo_scheduler_init(struct dodo_scheduler *scheduler) {
    if (scheduler == NULL) {
        return;
    }

    for (uint32_t i = 0; i < DODO_TASK_CAPACITY; ++i) {
        scheduler->tasks[i] = (struct dodo_task){0};
    }
    scheduler->next_id = 1u;
    scheduler->current_id = DODO_TASK_INVALID_ID;
    scheduler->cursor = DODO_TASK_CAPACITY - 1u;
    scheduler->task_count = 0u;
}

enum dodo_task_status dodo_scheduler_validate(
    const struct dodo_scheduler *scheduler) {
    if (scheduler == NULL || scheduler->next_id == 0u ||
        scheduler->task_count > DODO_TASK_CAPACITY ||
        scheduler->cursor >= DODO_TASK_CAPACITY) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    uint32_t active_count = 0u;
    uint32_t running_count = 0u;
    uint32_t running_id = DODO_TASK_INVALID_ID;

    for (uint32_t i = 0u; i < DODO_TASK_CAPACITY; ++i) {
        const struct dodo_task *task = &scheduler->tasks[i];
        if (task->state == DODO_TASK_UNUSED) {
            /*
             * A free slot must be fully cleared. Otherwise stale task metadata
             * can survive reaping and later be mistaken for live resources.
             */
            if (task->id != DODO_TASK_INVALID_ID ||
                task->saved_stack_pointer != 0u ||
                task->address_space_root != 0u ||
                task->exit_code != 0u || task->name[0] != '\\0') {
                return DODO_TASK_INVALID_ARGUMENT;
            }
            continue;
        }

        if (task->state < DODO_TASK_READY || task->state > DODO_TASK_ZOMBIE ||
            task->id == DODO_TASK_INVALID_ID || task->id >= scheduler->next_id ||
            task->saved_stack_pointer == 0u || task->address_space_root == 0u) {
            return DODO_TASK_INVALID_ARGUMENT;
        }

        /* Every active task name must terminate within its fixed-size field. */
        uint32_t name_length = 0u;
        while (name_length <= DODO_TASK_NAME_MAX &&
               task->name[name_length] != '\\0') {
            ++name_length;
        }
        if (name_length > DODO_TASK_NAME_MAX) {
            return DODO_TASK_INVALID_ARGUMENT;
        }
        ++active_count;

        for (uint32_t j = i + 1u; j < DODO_TASK_CAPACITY; ++j) {
            if (scheduler->tasks[j].state != DODO_TASK_UNUSED &&
                scheduler->tasks[j].id == task->id) {
                return DODO_TASK_INVALID_ARGUMENT;
            }
        }

        if (task->state == DODO_TASK_RUNNING) {
            ++running_count;
            running_id = task->id;
        }
    }

    if (active_count != scheduler->task_count || running_count > 1u ||
        (scheduler->current_id == DODO_TASK_INVALID_ID && running_count != 0u) ||
        (scheduler->current_id != DODO_TASK_INVALID_ID &&
         (running_count != 1u || running_id != scheduler->current_id))) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    return DODO_TASK_OK;
}

struct dodo_task *dodo_scheduler_find(
    struct dodo_scheduler *scheduler, uint32_t task_id) {
    if (!valid_scheduler(scheduler) || task_id == DODO_TASK_INVALID_ID) {
        return NULL;
    }

    for (uint32_t i = 0; i < DODO_TASK_CAPACITY; ++i) {
        if (scheduler->tasks[i].state != DODO_TASK_UNUSED &&
            scheduler->tasks[i].id == task_id) {
            return &scheduler->tasks[i];
        }
    }
    return NULL;
}

enum dodo_task_status dodo_scheduler_create(
    struct dodo_scheduler *scheduler,
    const char *name,
    uint64_t saved_stack_pointer,
    uint64_t address_space_root,
    uint32_t *task_id) {
    if (!valid_scheduler(scheduler) || name == NULL || task_id == NULL ||
        saved_stack_pointer == 0u || address_space_root == 0u || name[0] == '\0') {
        return DODO_TASK_INVALID_ARGUMENT;
    }
    if (scheduler->next_id == UINT32_MAX) {
        return DODO_TASK_FULL;
    }

    uint32_t slot = DODO_TASK_CAPACITY;
    for (uint32_t i = 0; i < DODO_TASK_CAPACITY; ++i) {
        if (scheduler->tasks[i].state == DODO_TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == DODO_TASK_CAPACITY) {
        return DODO_TASK_FULL;
    }

    struct dodo_task task = {
        .id = scheduler->next_id,
        .state = DODO_TASK_READY,
        .saved_stack_pointer = saved_stack_pointer,
        .address_space_root = address_space_root,
        .exit_code = 0u
    };
    uint32_t length = 0u;
    while (length < DODO_TASK_NAME_MAX && name[length] != '\0') {
        task.name[length] = name[length];
        ++length;
    }
    task.name[length] = '\0';

    scheduler->tasks[slot] = task;
    *task_id = scheduler->next_id;
    ++scheduler->next_id;
    ++scheduler->task_count;
    return DODO_TASK_OK;
}

enum dodo_task_status dodo_scheduler_schedule(
    struct dodo_scheduler *scheduler, uint32_t *task_id) {
    if (!valid_scheduler(scheduler) || task_id == NULL) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    struct dodo_task *previous = dodo_scheduler_find(
        scheduler, scheduler->current_id);
    if (previous != NULL && previous->state == DODO_TASK_RUNNING) {
        previous->state = DODO_TASK_READY;
    }

    for (uint32_t offset = 1u; offset <= DODO_TASK_CAPACITY; ++offset) {
        const uint32_t slot =
            (scheduler->cursor + offset) % DODO_TASK_CAPACITY;
        struct dodo_task *candidate = &scheduler->tasks[slot];
        if (candidate->state == DODO_TASK_READY) {
            candidate->state = DODO_TASK_RUNNING;
            scheduler->cursor = slot;
            scheduler->current_id = candidate->id;
            *task_id = candidate->id;
            return DODO_TASK_OK;
        }
    }

    scheduler->current_id = DODO_TASK_INVALID_ID;
    return DODO_TASK_NO_RUNNABLE_TASK;
}

enum dodo_task_status dodo_scheduler_save_stack_pointer(
    struct dodo_scheduler *scheduler,
    uint32_t task_id,
    uint64_t saved_stack_pointer) {
    if (!valid_scheduler(scheduler) || task_id == DODO_TASK_INVALID_ID ||
        saved_stack_pointer == 0u) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    struct dodo_task *task = dodo_scheduler_find(scheduler, task_id);
    if (task == NULL) {
        return DODO_TASK_NOT_FOUND;
    }
    if (scheduler->current_id != task_id || task->state != DODO_TASK_RUNNING) {
        return DODO_TASK_INVALID_STATE;
    }

    task->saved_stack_pointer = saved_stack_pointer;
    return DODO_TASK_OK;
}

enum dodo_task_status dodo_scheduler_set_current_state(
    struct dodo_scheduler *scheduler,
    enum dodo_task_state state,
    uint32_t exit_code) {
    if (!valid_scheduler(scheduler) ||
        (state != DODO_TASK_READY && state != DODO_TASK_BLOCKED &&
         state != DODO_TASK_ZOMBIE)) {
        return DODO_TASK_INVALID_ARGUMENT;
    }
    struct dodo_task *current =
        dodo_scheduler_find(scheduler, scheduler->current_id);
    if (current == NULL || current->state != DODO_TASK_RUNNING) {
        return DODO_TASK_INVALID_STATE;
    }

    current->state = state;
    if (state == DODO_TASK_ZOMBIE) {
        current->exit_code = exit_code;
    }
    scheduler->current_id = DODO_TASK_INVALID_ID;
    return DODO_TASK_OK;
}

enum dodo_task_status dodo_scheduler_wake(
    struct dodo_scheduler *scheduler, uint32_t task_id) {
    /*
     * Keep malformed scheduler state distinct from a valid scheduler that
     * simply has no task with this ID. This makes caller error handling sane.
     */
    if (!valid_scheduler(scheduler)) {
        return DODO_TASK_INVALID_ARGUMENT;
    }
    struct dodo_task *task = dodo_scheduler_find(scheduler, task_id);
    if (task == NULL) {
        return DODO_TASK_NOT_FOUND;
    }
    if (task->state != DODO_TASK_BLOCKED) {
        return DODO_TASK_INVALID_STATE;
    }
    task->state = DODO_TASK_READY;
    return DODO_TASK_OK;
}

enum dodo_task_status dodo_scheduler_get_exit_code(
    struct dodo_scheduler *scheduler,
    uint32_t task_id,
    uint32_t *exit_code) {
    if (!valid_scheduler(scheduler) || task_id == DODO_TASK_INVALID_ID ||
        exit_code == NULL) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    const struct dodo_task *task = dodo_scheduler_find(scheduler, task_id);
    if (task == NULL) {
        return DODO_TASK_NOT_FOUND;
    }
    if (task->state != DODO_TASK_ZOMBIE) {
        return DODO_TASK_INVALID_STATE;
    }

    *exit_code = task->exit_code;
    return DODO_TASK_OK;
}

enum dodo_task_status dodo_scheduler_reap(
    struct dodo_scheduler *scheduler, uint32_t task_id) {
    if (!valid_scheduler(scheduler) || task_id == DODO_TASK_INVALID_ID) {
        return DODO_TASK_INVALID_ARGUMENT;
    }

    struct dodo_task *task = dodo_scheduler_find(scheduler, task_id);
    if (task == NULL) {
        return DODO_TASK_NOT_FOUND;
    }
    if (task->state != DODO_TASK_ZOMBIE) {
        return DODO_TASK_INVALID_STATE;
    }

    *task = (struct dodo_task){0};
    --scheduler->task_count;
    return DODO_TASK_OK;
}
