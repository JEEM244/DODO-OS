#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dodo/kernel/task.h"

static void test_round_robin_and_states(void) {
    struct dodo_scheduler scheduler;
    dodo_scheduler_init(&scheduler);
    uint32_t a = 0u, b = 0u, c = 0u, selected = 0u;
    assert(dodo_scheduler_create(&scheduler, "init", 0x1000u, 0x2000u, &a) == DODO_TASK_OK);
    assert(dodo_scheduler_create(&scheduler, "worker", 0x3000u, 0x4000u, &b) == DODO_TASK_OK);
    assert(dodo_scheduler_create(&scheduler, "logger", 0x5000u, 0x6000u, &c) == DODO_TASK_OK);
    assert(a != b && b != c && a != c);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == a);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == b);
    assert(dodo_scheduler_set_current_state(&scheduler, DODO_TASK_BLOCKED, 0u) == DODO_TASK_OK);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == c);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == a);
    assert(dodo_scheduler_wake(&scheduler, b) == DODO_TASK_OK);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == b);
    assert(dodo_scheduler_set_current_state(&scheduler, DODO_TASK_ZOMBIE, 17u) == DODO_TASK_OK);
    assert(dodo_scheduler_find(&scheduler, b)->exit_code == 17u);
    assert(dodo_scheduler_schedule(&scheduler, &selected) == DODO_TASK_OK && selected == c);
    assert(dodo_scheduler_find(&scheduler, b)->state == DODO_TASK_ZOMBIE);
}

static void test_validation_and_capacity(void) {
    struct dodo_scheduler scheduler;
    dodo_scheduler_init(&scheduler);
    uint32_t id = 0u;
    assert(dodo_scheduler_create(&scheduler, "", 0x1000u, 0x2000u, &id) == DODO_TASK_INVALID_ARGUMENT);
    assert(dodo_scheduler_create(&scheduler, "bad", 0u, 0x2000u, &id) == DODO_TASK_INVALID_ARGUMENT);
    assert(dodo_scheduler_create(&scheduler, "bad", 0x1000u, 0u, &id) == DODO_TASK_INVALID_ARGUMENT);
    for (uint32_t i = 0u; i < DODO_TASK_CAPACITY; ++i) {
        assert(dodo_scheduler_create(&scheduler, "task", 0x1000u + i * 0x1000u,
            0x2000u + i * 0x1000u, &id) == DODO_TASK_OK);
    }
    assert(scheduler.task_count == DODO_TASK_CAPACITY);
    assert(dodo_scheduler_create(&scheduler, "overflow", 0x90000u, 0xa0000u, &id) == DODO_TASK_FULL);
    assert(dodo_scheduler_schedule(&scheduler, &id) == DODO_TASK_OK);
    assert(dodo_scheduler_set_current_state(&scheduler, DODO_TASK_BLOCKED, 0u) == DODO_TASK_OK);
    assert(dodo_scheduler_schedule(&scheduler, &id) == DODO_TASK_OK);
    assert(dodo_scheduler_wake(&scheduler, UINT32_C(99999)) == DODO_TASK_NOT_FOUND);
    assert(dodo_scheduler_wake(&scheduler, id) == DODO_TASK_INVALID_STATE);
}

int main(void) {
    test_round_robin_and_states();
    test_validation_and_capacity();
    puts("Task scheduler tests passed.");
    return 0;
}
