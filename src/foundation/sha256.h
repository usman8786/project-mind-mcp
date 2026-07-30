#ifndef PMM_SHA256_H
#define PMM_SHA256_H

/* In-process SHA-256 (FIPS 180-4). Used to verify the integrity of a
 * downloaded release before installing it, without shelling out to a
 * platform hashing tool (shasum / sha256sum / certutil) — those differ per
 * OS, may be absent, and mis-quote paths under cmd.exe. */

#include <stddef.h>
#include <stdint.h>

#define PMM_SHA256_DIGEST_LEN 32 /* raw digest bytes */
#define PMM_SHA256_HEX_LEN 64    /* lowercase hex chars (no NUL) */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buf[64];
    size_t buflen;
} pmm_sha256_ctx;

void pmm_sha256_init(pmm_sha256_ctx *c);
void pmm_sha256_update(pmm_sha256_ctx *c, const void *data, size_t len);
void pmm_sha256_final(pmm_sha256_ctx *c, uint8_t out[PMM_SHA256_DIGEST_LEN]);

/* One-shot hash of a buffer to lowercase hex. `out` must hold
 * PMM_SHA256_HEX_LEN + 1 bytes (hex chars + NUL). */
void pmm_sha256_hex(const void *data, size_t len, char out[PMM_SHA256_HEX_LEN + 1]);

#endif /* PMM_SHA256_H */
