#ifndef FLUX_MEM_POISON_GUARDPAGE_H
#define FLUX_MEM_POISON_GUARDPAGE_H

#include "types.h"

typedef struct {
    uint32_t guard_id;
    uint8_t  poison_byte;
    uint8_t  redzone_bytes;
    uint16_t quarantine_slots;
    const char *allocator_target;
    const char *canary_formula;
    const char *corruption_response;
} mem_poison_rule_t;

void   mem_poison_guardpage_init(void);
void  *guarded_alloc(size_t payload_size);
bool   guarded_verify_and_free(void *payload_ptr);
size_t mem_poison_db_size(void);
bool   mem_poison_guardpage_selftest(void);

#endif
