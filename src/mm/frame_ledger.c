#include <stddef.h>
#include <stdint.h>

#include "dodo/mm/frame_ledger.h"

static int valid_kind(enum dodo_frame_kind kind) {
    return kind >= DODO_FRAME_KIND_USER_DATA && kind <= DODO_FRAME_KIND_ROOT;
}

void dodo_frame_ledger_init(struct dodo_frame_ledger *ledger) {
    if (ledger == NULL) {
        return;
    }
    ledger->count = 0u;
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        ledger->records[i] = (struct dodo_frame_record){0};
    }
}

enum dodo_frame_ledger_status dodo_frame_ledger_validate(
    const struct dodo_frame_ledger *ledger) {
    if (ledger == NULL || ledger->count > DODO_FRAME_LEDGER_CAPACITY) {
        return DODO_FRAME_LEDGER_INVALID_ARGUMENT;
    }
    uint32_t active = 0u;
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        const struct dodo_frame_record *record = &ledger->records[i];
        if (record->physical_address == 0u && record->owner_id == 0u &&
            record->kind == DODO_FRAME_KIND_UNUSED) {
            continue;
        }
        if (record->physical_address == 0u ||
            (record->physical_address & UINT64_C(4095)) != 0u ||
            record->owner_id == DODO_FRAME_OWNER_INVALID ||
            !valid_kind(record->kind)) {
            return DODO_FRAME_LEDGER_INVALID_STATE;
        }
        ++active;
        for (uint32_t j = i + 1u; j < DODO_FRAME_LEDGER_CAPACITY; ++j) {
            const struct dodo_frame_record *other = &ledger->records[j];
            if (other->physical_address == record->physical_address) {
                return DODO_FRAME_LEDGER_INVALID_STATE;
            }
        }
    }
    return active == ledger->count ? DODO_FRAME_LEDGER_OK :
        DODO_FRAME_LEDGER_INVALID_STATE;
}

enum dodo_frame_ledger_status dodo_frame_ledger_add(
    struct dodo_frame_ledger *ledger,
    uint64_t physical_address,
    uint32_t owner_id,
    enum dodo_frame_kind kind) {
    if (dodo_frame_ledger_validate(ledger) != DODO_FRAME_LEDGER_OK ||
        physical_address == 0u || (physical_address & UINT64_C(4095)) != 0u ||
        owner_id == DODO_FRAME_OWNER_INVALID || !valid_kind(kind)) {
        return DODO_FRAME_LEDGER_INVALID_ARGUMENT;
    }
    uint32_t free_slot = DODO_FRAME_LEDGER_CAPACITY;
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (ledger->records[i].physical_address == physical_address) {
            return DODO_FRAME_LEDGER_DUPLICATE_FRAME;
        }
        if (free_slot == DODO_FRAME_LEDGER_CAPACITY &&
            ledger->records[i].kind == DODO_FRAME_KIND_UNUSED) {
            free_slot = i;
        }
    }
    if (free_slot == DODO_FRAME_LEDGER_CAPACITY) {
        return DODO_FRAME_LEDGER_FULL;
    }
    ledger->records[free_slot] = (struct dodo_frame_record){
        .physical_address = physical_address,
        .owner_id = owner_id,
        .kind = kind
    };
    ++ledger->count;
    return DODO_FRAME_LEDGER_OK;
}

enum dodo_frame_ledger_status dodo_frame_ledger_remove(
    struct dodo_frame_ledger *ledger,
    uint64_t physical_address,
    uint32_t owner_id,
    enum dodo_frame_kind *kind_out) {
    if (dodo_frame_ledger_validate(ledger) != DODO_FRAME_LEDGER_OK ||
        physical_address == 0u || (physical_address & UINT64_C(4095)) != 0u ||
        owner_id == DODO_FRAME_OWNER_INVALID) {
        return DODO_FRAME_LEDGER_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        struct dodo_frame_record *record = &ledger->records[i];
        if (record->physical_address != physical_address) {
            continue;
        }
        if (record->owner_id != owner_id) {
            return DODO_FRAME_LEDGER_INVALID_STATE;
        }
        if (kind_out != NULL) {
            *kind_out = record->kind;
        }
        *record = (struct dodo_frame_record){0};
        --ledger->count;
        return DODO_FRAME_LEDGER_OK;
    }
    return DODO_FRAME_LEDGER_NOT_FOUND;
}

enum dodo_frame_ledger_status dodo_frame_ledger_count_owner(
    const struct dodo_frame_ledger *ledger,
    uint32_t owner_id,
    uint32_t *count_out) {
    if (dodo_frame_ledger_validate(ledger) != DODO_FRAME_LEDGER_OK ||
        owner_id == DODO_FRAME_OWNER_INVALID || count_out == NULL) {
        return DODO_FRAME_LEDGER_INVALID_ARGUMENT;
    }
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (ledger->records[i].kind != DODO_FRAME_KIND_UNUSED &&
            ledger->records[i].owner_id == owner_id) {
            ++count;
        }
    }
    *count_out = count;
    return DODO_FRAME_LEDGER_OK;
}
