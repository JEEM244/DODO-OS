#ifndef DODO_MM_FRAME_LEDGER_H
#define DODO_MM_FRAME_LEDGER_H

#include <stdint.h>

#define DODO_FRAME_LEDGER_CAPACITY 512u
#define DODO_FRAME_OWNER_INVALID UINT32_C(0)

enum dodo_frame_kind {
    DODO_FRAME_KIND_UNUSED = 0,
    DODO_FRAME_KIND_USER_DATA,
    DODO_FRAME_KIND_PAGE_TABLE,
    DODO_FRAME_KIND_ROOT
};

enum dodo_frame_ledger_status {
    DODO_FRAME_LEDGER_OK = 0,
    DODO_FRAME_LEDGER_INVALID_ARGUMENT,
    DODO_FRAME_LEDGER_FULL,
    DODO_FRAME_LEDGER_DUPLICATE_FRAME,
    DODO_FRAME_LEDGER_NOT_FOUND,
    DODO_FRAME_LEDGER_INVALID_STATE
};

struct dodo_frame_record {
    uint64_t physical_address;
    uint32_t owner_id;
    enum dodo_frame_kind kind;
};

struct dodo_frame_ledger {
    struct dodo_frame_record records[DODO_FRAME_LEDGER_CAPACITY];
    uint32_t count;
};

/* Fixed-capacity metadata only: this ledger never allocates or frees RAM. */
void dodo_frame_ledger_init(struct dodo_frame_ledger *ledger);

enum dodo_frame_ledger_status dodo_frame_ledger_validate(
    const struct dodo_frame_ledger *ledger);

enum dodo_frame_ledger_status dodo_frame_ledger_add(
    struct dodo_frame_ledger *ledger,
    uint64_t physical_address,
    uint32_t owner_id,
    enum dodo_frame_kind kind);

enum dodo_frame_ledger_status dodo_frame_ledger_remove(
    struct dodo_frame_ledger *ledger,
    uint64_t physical_address,
    uint32_t owner_id,
    enum dodo_frame_kind *kind_out);

/* Count records owned by one address-space/task ID. */
enum dodo_frame_ledger_status dodo_frame_ledger_count_owner(
    const struct dodo_frame_ledger *ledger,
    uint32_t owner_id,
    uint32_t *count_out);

#endif
