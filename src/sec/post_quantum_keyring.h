#ifndef FLUX_POST_QUANTUM_KEYRING_H
#define FLUX_POST_QUANTUM_KEYRING_H

#include "types.h"

typedef struct {
    uint32_t pq_rule_id;
    uint8_t  crypto_suite; /* 1=ML-KEM-1024-Kyber, 2=ML-DSA-87-Dilithium, 3=Kernel-Keyring-fscrypt, 4=USBGuard-SEV-TDX */
    uint8_t  nist_level;   /* NIST Security Category 5 (AES-256 equivalent) */
    uint16_t key_size_bytes;
    const char *keyring_domain;
    const char *lattice_or_hw_primitive;
    const char *quantum_resistance_policy;
} post_quantum_rule_t;

typedef struct {
    bool     ml_kem_1024_kyber_ready;
    bool     ml_dsa_87_dilithium_sig;
    bool     kernel_keyring_fscrypt;
    bool     usbguard_badusb_blocker;
    uint32_t active_pq_keys;
} post_quantum_keyring_status_t;

void   post_quantum_keyring_init(void);
bool   post_quantum_get_status(post_quantum_keyring_status_t *out);
size_t post_quantum_db_size(void);
bool   post_quantum_keyring_selftest(void);

#endif
