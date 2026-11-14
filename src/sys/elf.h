#ifndef FLUX_ELF_H
#define FLUX_ELF_H

#include "types.h"
#include "vmm.h"

#define ET_EXEC           2U
#define ET_DYN            3U
#define PT_DYNAMIC        2U
#define PT_GNU_STACK      0x6474e551U
#define PT_GNU_RELRO      0x6474e552U
#define DT_NULL           0
#define DT_RELA           7
#define DT_RELASZ         8
#define DT_RELAENT        9
#define R_X86_64_RELATIVE 8U

typedef struct PACKED {
    int64_t  d_tag;
    uint64_t d_val;
} elf64_dyn_t;

typedef struct PACKED {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} elf64_rela_t;

bool elf_is_valid(const uint8_t *buf, size_t len);
bool elf_load_into_vm(const uint8_t *buf, size_t len,
                      int argc, const char *const *argv,
                      vm_space_t **out_vm, uint64_t *out_entry, uint64_t *out_rsp);

#endif
