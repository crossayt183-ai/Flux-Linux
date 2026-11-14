#ifndef FLUX_SMP_ACPI_H
#define FLUX_SMP_ACPI_H

#include "types.h"

typedef enum {
    ACPI_PROFILE_POWER_SAVER = 0,
    ACPI_PROFILE_BALANCED    = 1,
    ACPI_PROFILE_PERFORMANCE = 2,
    ACPI_PROFILE_K_OVERCLOCK = 3  /* Unlocked 'K' Multiplier Overclock (e.g. i7-2600K @ 4.6 GHz) */
} acpi_power_profile_t;

typedef enum {
    ACPI_SLEEP_S0_ACTIVE  = 0,
    ACPI_SLEEP_S0IX_IDLE  = 1,
    ACPI_SLEEP_S3_RAM     = 3,
    ACPI_SLEEP_S4_DISK    = 4,
    ACPI_SLEEP_S5_SOFTOFF = 5
} acpi_sleep_state_t;

typedef struct {
    uint8_t  apic_id;
    uint8_t  core_id;
    uint8_t  ccd_id;
    bool     online;
    bool     has_3d_vcache;
    bool     is_p_core;
    uint32_t cur_freq_mhz;
    uint32_t max_boost_mhz;
    uint32_t temp_celsius;
    uint64_t dispatched_tasks;
} smp_cpu_core_t;

typedef struct {
    uint32_t             online_cores;
    uint32_t             total_threads;
    uint32_t             l3_cache_mib;
    uint32_t             package_watts;
    uint32_t             package_temp_c;
    uint16_t             release_year;
    acpi_power_profile_t power_profile;
    acpi_sleep_state_t   sleep_state;
    char                 cpu_brand[64];
    char                 arch_codename[40];
    char                 isa_extensions[48];
    char                 scaling_driver[40];
    char                 acpi_oem_id[24];
} smp_acpi_status_t;

void   smp_acpi_init(void);
bool   smp_acpi_get_status(smp_acpi_status_t *out);
size_t smp_cpu_core_count(void);
bool   smp_get_cpu_core(size_t idx, smp_cpu_core_t *out);
bool   acpi_set_power_profile(acpi_power_profile_t prof);
bool   smp_switch_cpu_profile(bool use_i7_2600k);
size_t smp_cpu_db_size(void);
bool   smp_acpi_selftest(void);

#endif
