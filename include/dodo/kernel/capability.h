#ifndef DODO_KERNEL_CAPABILITY_H
#define DODO_KERNEL_CAPABILITY_H
#include <stdint.h>
#define DODO_CAPABILITY_CAPACITY 64u
#define DODO_CAPABILITY_RIGHTS_MASK UINT32_C(0x3f)

enum dodo_capability_object_type {
    DODO_CAPABILITY_OBJECT_INVALID = 0,
    DODO_CAPABILITY_OBJECT_MEMORY,
    DODO_CAPABILITY_OBJECT_DEVICE,
    DODO_CAPABILITY_OBJECT_FILE,
    DODO_CAPABILITY_OBJECT_DIRECTORY,
    DODO_CAPABILITY_OBJECT_NETWORK_ENDPOINT,
    DODO_CAPABILITY_OBJECT_IPC_ENDPOINT
};
enum dodo_capability_right {
    DODO_CAPABILITY_READ = UINT32_C(1) << 0,
    DODO_CAPABILITY_WRITE = UINT32_C(1) << 1,
    DODO_CAPABILITY_EXECUTE = UINT32_C(1) << 2,
    DODO_CAPABILITY_CONNECT = UINT32_C(1) << 3,
    DODO_CAPABILITY_ACCEPT = UINT32_C(1) << 4,
    DODO_CAPABILITY_CONTROL = UINT32_C(1) << 5
};
enum dodo_capability_status {
    DODO_CAPABILITY_OK = 0,
    DODO_CAPABILITY_INVALID_ARGUMENT,
    DODO_CAPABILITY_FULL,
    DODO_CAPABILITY_DENIED,
    DODO_CAPABILITY_NOT_FOUND
};
struct dodo_capability_entry {
    uint32_t task_id;
    enum dodo_capability_object_type object_type;
    uint64_t object_id;
    uint32_t rights;
};
struct dodo_capability_table {
    struct dodo_capability_entry entries[DODO_CAPABILITY_CAPACITY];
    uint32_t count;
};
void dodo_capability_table_init(struct dodo_capability_table *table);
enum dodo_capability_status dodo_capability_table_validate(const struct dodo_capability_table *table);
enum dodo_capability_status dodo_capability_grant(struct dodo_capability_table *table, uint32_t task_id, enum dodo_capability_object_type type, uint64_t object_id, uint32_t rights);
enum dodo_capability_status dodo_capability_check(const struct dodo_capability_table *table, uint32_t task_id, enum dodo_capability_object_type type, uint64_t object_id, uint32_t rights);
enum dodo_capability_status dodo_capability_revoke(struct dodo_capability_table *table, uint32_t task_id, enum dodo_capability_object_type type, uint64_t object_id, uint32_t rights);
enum dodo_capability_status dodo_capability_revoke_task(struct dodo_capability_table *table, uint32_t task_id);
#endif
