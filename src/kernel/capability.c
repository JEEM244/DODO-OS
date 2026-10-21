#include "dodo/kernel/capability.h"

static int valid_type(enum dodo_capability_object_type type) {
    return type >= DODO_CAPABILITY_OBJECT_MEMORY && type <= DODO_CAPABILITY_OBJECT_IPC_ENDPOINT;
}
static int valid_rights(uint32_t rights) {
    return rights != 0u && (rights & ~DODO_CAPABILITY_RIGHTS_MASK) == 0u;
}
static int valid_request(uint32_t task, enum dodo_capability_object_type type,
                         uint64_t object, uint32_t rights) {
    return task != 0u && valid_type(type) && object != 0u && valid_rights(rights);
}
static int same_key(const struct dodo_capability_entry *e, uint32_t task,
                    enum dodo_capability_object_type type, uint64_t object) {
    return e->task_id == task && e->object_type == type && e->object_id == object;
}
static void clear_entry(struct dodo_capability_entry *e) {
    *e = (struct dodo_capability_entry){0};
}
void dodo_capability_table_init(struct dodo_capability_table *table) {
    if (table == 0) return;
    for (uint32_t i = 0; i < DODO_CAPABILITY_CAPACITY; ++i) clear_entry(&table->entries[i]);
    table->count = 0u;
}
enum dodo_capability_status dodo_capability_table_validate(const struct dodo_capability_table *table) {
    if (table == 0 || table->count > DODO_CAPABILITY_CAPACITY) return DODO_CAPABILITY_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct dodo_capability_entry *e = &table->entries[i];
        if (!valid_request(e->task_id, e->object_type, e->object_id, e->rights))
            return DODO_CAPABILITY_INVALID_ARGUMENT;
        for (uint32_t j = i + 1u; j < table->count; ++j)
            if (same_key(&table->entries[j], e->task_id, e->object_type, e->object_id))
                return DODO_CAPABILITY_INVALID_ARGUMENT;
    }
    for (uint32_t i = table->count; i < DODO_CAPABILITY_CAPACITY; ++i) {
        const struct dodo_capability_entry *e = &table->entries[i];
        if (e->task_id != 0u || e->object_type != DODO_CAPABILITY_OBJECT_INVALID ||
            e->object_id != 0u || e->rights != 0u) return DODO_CAPABILITY_INVALID_ARGUMENT;
    }
    return DODO_CAPABILITY_OK;
}
enum dodo_capability_status dodo_capability_grant(struct dodo_capability_table *table,
    uint32_t task, enum dodo_capability_object_type type, uint64_t object, uint32_t rights) {
    if (dodo_capability_table_validate(table) != DODO_CAPABILITY_OK ||
        !valid_request(task, type, object, rights)) return DODO_CAPABILITY_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < table->count; ++i) {
        struct dodo_capability_entry *e = &table->entries[i];
        if (same_key(e, task, type, object)) { e->rights |= rights; return DODO_CAPABILITY_OK; }
    }
    if (table->count == DODO_CAPABILITY_CAPACITY) return DODO_CAPABILITY_FULL;
    table->entries[table->count++] = (struct dodo_capability_entry){task, type, object, rights};
    return DODO_CAPABILITY_OK;
}
enum dodo_capability_status dodo_capability_check(const struct dodo_capability_table *table,
    uint32_t task, enum dodo_capability_object_type type, uint64_t object, uint32_t rights) {
    if (dodo_capability_table_validate(table) != DODO_CAPABILITY_OK ||
        !valid_request(task, type, object, rights)) return DODO_CAPABILITY_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct dodo_capability_entry *e = &table->entries[i];
        if (same_key(e, task, type, object))
            return (e->rights & rights) == rights ? DODO_CAPABILITY_OK : DODO_CAPABILITY_DENIED;
    }
    return DODO_CAPABILITY_DENIED;
}
enum dodo_capability_status dodo_capability_revoke(struct dodo_capability_table *table,
    uint32_t task, enum dodo_capability_object_type type, uint64_t object, uint32_t rights) {
    if (dodo_capability_table_validate(table) != DODO_CAPABILITY_OK ||
        !valid_request(task, type, object, rights)) return DODO_CAPABILITY_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < table->count; ++i) {
        struct dodo_capability_entry *e = &table->entries[i];
        if (!same_key(e, task, type, object)) continue;
        if ((e->rights & rights) == 0u) return DODO_CAPABILITY_DENIED;
        const uint32_t remaining = e->rights & ~rights;
        if (remaining != 0u) { e->rights = remaining; return DODO_CAPABILITY_OK; }
        const uint32_t last = table->count - 1u;
        if (i != last) table->entries[i] = table->entries[last];
        clear_entry(&table->entries[last]);
        --table->count;
        return DODO_CAPABILITY_OK;
    }
    return DODO_CAPABILITY_NOT_FOUND;
}
enum dodo_capability_status dodo_capability_revoke_task(struct dodo_capability_table *table, uint32_t task) {
    if (dodo_capability_table_validate(table) != DODO_CAPABILITY_OK || task == 0u)
        return DODO_CAPABILITY_INVALID_ARGUMENT;
    uint32_t removed = 0u, i = 0u;
    while (i < table->count) {
        if (table->entries[i].task_id != task) { ++i; continue; }
        const uint32_t last = table->count - 1u;
        if (i != last) table->entries[i] = table->entries[last];
        clear_entry(&table->entries[last]);
        --table->count;
        ++removed;
    }
    return removed ? DODO_CAPABILITY_OK : DODO_CAPABILITY_NOT_FOUND;
}
