#ifndef FLUX_IPC_FUTEX_DBUS_H
#define FLUX_IPC_FUTEX_DBUS_H

#include "types.h"

typedef struct {
    uint16_t channel_id;
    uint8_t  ipc_kind; /* 1=futex_waitv, 2=memfd_shm, 3=kdbus_broker, 4=posix_mqueue, 5=pipe_splice */
    uint8_t  priority_boost;
    uint32_t ring_capacity_kb;
    const char *endpoint_name;
    const char *sync_primitive;
    const char *zero_copy_mechanism;
} ipc_channel_rule_t;

typedef struct {
    bool     futex2_waitv_online;
    bool     kdbus_zero_copy_bus;
    bool     memfd_sealing_ready;
    uint32_t active_ipc_channels;
    uint64_t futex_wakeups_fast;
} ipc_subsystem_status_t;

void   ipc_futex_dbus_init(void);
bool   ipc_futex_dbus_get_status(ipc_subsystem_status_t *out);
size_t ipc_channel_db_size(void);
bool   ipc_futex_dbus_selftest(void);

#endif
