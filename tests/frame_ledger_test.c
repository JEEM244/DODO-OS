#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "dodo/mm/frame_ledger.h"

static void test_add_remove_and_owner_count(void) {
    struct dodo_frame_ledger ledger;
    enum dodo_frame_kind kind = DODO_FRAME_KIND_UNUSED;
    uint32_t count = 99u;
    dodo_frame_ledger_init(&ledger);
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_OK);
    assert(dodo_frame_ledger_add(&ledger, 0x1000u, 1u,
        DODO_FRAME_KIND_ROOT) == DODO_FRAME_LEDGER_OK);
    assert(dodo_frame_ledger_add(&ledger, 0x2000u, 1u,
        DODO_FRAME_KIND_PAGE_TABLE) == DODO_FRAME_LEDGER_OK);
    assert(dodo_frame_ledger_add(&ledger, 0x3000u, 2u,
        DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_OK);
    assert(dodo_frame_ledger_count_owner(&ledger, 1u, &count) == DODO_FRAME_LEDGER_OK);
    assert(count == 2u);
    assert(dodo_frame_ledger_remove(&ledger, 0x2000u, 1u, &kind) == DODO_FRAME_LEDGER_OK);
    assert(kind == DODO_FRAME_KIND_PAGE_TABLE);
    assert(dodo_frame_ledger_remove(&ledger, 0x2000u, 1u, NULL) == DODO_FRAME_LEDGER_NOT_FOUND);
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_OK);
}

static void test_reject_invalid_and_duplicate_frames(void) {
    struct dodo_frame_ledger ledger;
    dodo_frame_ledger_init(&ledger);
    assert(dodo_frame_ledger_add(&ledger, 0u, 1u,
        DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_INVALID_ARGUMENT);
    assert(dodo_frame_ledger_add(&ledger, 0x1001u, 1u,
        DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_INVALID_ARGUMENT);
    assert(dodo_frame_ledger_add(&ledger, 0x1000u, 0u,
        DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_INVALID_ARGUMENT);
    assert(dodo_frame_ledger_add(&ledger, 0x1000u, 1u,
        DODO_FRAME_KIND_UNUSED) == DODO_FRAME_LEDGER_INVALID_ARGUMENT);
    assert(dodo_frame_ledger_add(&ledger, 0x1000u, 1u,
        DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_OK);
    assert(dodo_frame_ledger_add(&ledger, 0x1000u, 2u,
        DODO_FRAME_KIND_ROOT) == DODO_FRAME_LEDGER_DUPLICATE_FRAME);
    assert(dodo_frame_ledger_remove(&ledger, 0x1000u, 2u, NULL) ==
        DODO_FRAME_LEDGER_INVALID_STATE);
    assert(ledger.count == 1u);
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_OK);
}

static void test_capacity_and_corruption_detection(void) {
    struct dodo_frame_ledger ledger;
    dodo_frame_ledger_init(&ledger);
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        assert(dodo_frame_ledger_add(&ledger, ((uint64_t)i + 1u) * 4096u,
            i + 1u, DODO_FRAME_KIND_USER_DATA) == DODO_FRAME_LEDGER_OK);
    }
    assert(dodo_frame_ledger_add(&ledger, UINT64_C(0x1000000), 999u,
        DODO_FRAME_KIND_ROOT) == DODO_FRAME_LEDGER_FULL);
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_OK);
    ledger.records[3].physical_address = ledger.records[2].physical_address;
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_INVALID_STATE);
    dodo_frame_ledger_init(&ledger);
    ledger.count = DODO_FRAME_LEDGER_CAPACITY + 1u;
    assert(dodo_frame_ledger_validate(&ledger) == DODO_FRAME_LEDGER_INVALID_ARGUMENT);
}

int main(void) {
    test_add_remove_and_owner_count();
    test_reject_invalid_and_duplicate_frames();
    test_capacity_and_corruption_detection();
    puts("PASS: frame ownership ledger tests");
    return 0;
}
