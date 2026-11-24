#ifndef FLUX_ACPI_THERMAL_RAPL_H
#define FLUX_ACPI_THERMAL_RAPL_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} acpi_thermal_rapl_rule_t;

void   acpi_thermal_rapl_init(void);
size_t acpi_thermal_rapl_db_size(void);
bool   acpi_thermal_rapl_selftest(void);

#endif
