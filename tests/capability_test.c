#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/kernel/capability.h"

static void test_deny_default_scope_and_revocation(void) {
    struct dodo_capability_table t;
    dodo_capability_table_init(&t);
    assert(dodo_capability_table_validate(&t) == DODO_CAPABILITY_OK);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_OK);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_OK);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_WRITE) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_check(&t, 2, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 42, DODO_CAPABILITY_READ) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_WRITE) == DODO_CAPABILITY_OK);
    assert(t.count == 1u);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ | DODO_CAPABILITY_WRITE) == DODO_CAPABILITY_OK);
    assert(dodo_capability_revoke(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_OK);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_READ) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_WRITE) == DODO_CAPABILITY_OK);
    assert(dodo_capability_revoke_task(&t, 1) == DODO_CAPABILITY_OK);
    assert(dodo_capability_check(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 41, DODO_CAPABILITY_WRITE) == DODO_CAPABILITY_DENIED);
    assert(dodo_capability_table_validate(&t) == DODO_CAPABILITY_OK);
    assert(dodo_capability_revoke_task(&t, 1) == DODO_CAPABILITY_NOT_FOUND);
}
static void test_invalid_inputs_capacity_and_corruption(void) {
    struct dodo_capability_table t;
    dodo_capability_table_init(&t);
    assert(dodo_capability_table_validate(NULL) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(NULL, 1, DODO_CAPABILITY_OBJECT_FILE, 1, DODO_CAPABILITY_READ) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(&t, 0, DODO_CAPABILITY_OBJECT_FILE, 1, DODO_CAPABILITY_READ) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_INVALID, 1, DODO_CAPABILITY_READ) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 0, DODO_CAPABILITY_READ) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 1, 0) == DODO_CAPABILITY_INVALID_ARGUMENT);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 1, UINT32_C(0x80000000)) == DODO_CAPABILITY_INVALID_ARGUMENT);
    for (uint32_t i = 0; i < DODO_CAPABILITY_CAPACITY; ++i)
        assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 1000u + i, DODO_CAPABILITY_READ) == DODO_CAPABILITY_OK);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 9999, DODO_CAPABILITY_READ) == DODO_CAPABILITY_FULL);
    assert(dodo_capability_table_validate(&t) == DODO_CAPABILITY_OK);
    assert(dodo_capability_revoke_task(&t, 1) == DODO_CAPABILITY_OK);
    assert(t.count == 0u);
    assert(dodo_capability_table_validate(&t) == DODO_CAPABILITY_OK);
    assert(dodo_capability_grant(&t, 1, DODO_CAPABILITY_OBJECT_FILE, 1, DODO_CAPABILITY_READ) == DODO_CAPABILITY_OK);
    t.entries[t.count].object_id = 99;
    assert(dodo_capability_table_validate(&t) == DODO_CAPABILITY_INVALID_ARGUMENT);
}
int main(void) {
    test_deny_default_scope_and_revocation();
    test_invalid_inputs_capacity_and_corruption();
    puts("DODO capability table tests: PASS");
    return 0;
}
