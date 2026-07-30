/*
 * str_intern.h — String interning pool.
 *
 * Deduplicates strings: identical strings share a single allocation.
 * Returns stable pointers — safe to compare by pointer equality after interning.
 *
 * Uses an arena for string storage (bulk free) + hash table for dedup lookup.
 */
#ifndef PMM_STR_INTERN_H
#define PMM_STR_INTERN_H

#include <stddef.h>
#include <stdint.h>

typedef struct CBMInternPool CBMInternPool;

/* Create a new intern pool. */
CBMInternPool *pmm_intern_create(void);

/* Free the pool and all interned strings. */
void pmm_intern_free(CBMInternPool *pool);

/* Intern a NUL-terminated string. Returns a stable pointer.
 * The same input always returns the same pointer. */
const char *pmm_intern(CBMInternPool *pool, const char *s);

/* Intern a string of known length. */
const char *pmm_intern_n(CBMInternPool *pool, const char *s, size_t len);

/* Number of unique strings in the pool. */
uint32_t pmm_intern_count(const CBMInternPool *pool);

/* Total bytes stored (unique strings only). */
size_t pmm_intern_bytes(const CBMInternPool *pool);

#endif /* PMM_STR_INTERN_H */
