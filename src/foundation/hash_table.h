/*
 * hash_table.h — String → void* hash table.
 *
 * Public API unchanged across implementation rewrites. As of v2 the
 * internals are Verstable (https://github.com/JacksonAllan/Verstable),
 * a 2024 state-of-the-art open-addressing hash table with quadratic
 * probing + per-bucket 4-bit hash fragments. The struct is opaque —
 * callers MUST go through pmm_ht_create() and the API functions.
 *
 * Keys are borrowed pointers — the table does not copy or free them.
 * Callers own the key strings for the lifetime of the entry.
 */
#ifndef PMM_HASH_TABLE_H
#define PMM_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Opaque — full definition lives in hash_table.c. */
typedef struct CBMHashTable CBMHashTable;

/* Create a hash table with initial capacity hint (used to pre-reserve
 * buckets and avoid early growth; 0 = library default). */
CBMHashTable *pmm_ht_create(uint32_t initial_capacity);

/* Free the hash table (does NOT free keys or values). */
void pmm_ht_free(CBMHashTable *ht);

/* Insert or update. Returns previous value (NULL if new key). */
void *pmm_ht_set(CBMHashTable *ht, const char *key, void *value);

/* Lookup. Returns NULL if not found. */
void *pmm_ht_get(const CBMHashTable *ht, const char *key);

/* Check if key exists. */
bool pmm_ht_has(const CBMHashTable *ht, const char *key);

/* Return the stored key pointer for a given lookup key, or NULL.
 * Useful when you need the canonical (heap-owned) key string
 * rather than your own local copy. */
const char *pmm_ht_get_key(const CBMHashTable *ht, const char *key);

/* Delete. Returns removed value (NULL if not found). */
void *pmm_ht_delete(CBMHashTable *ht, const char *key);

/* Number of entries. */
uint32_t pmm_ht_count(const CBMHashTable *ht);

/* Iteration: call fn(key, value, userdata) for each entry. */
typedef void (*pmm_ht_iter_fn)(const char *key, void *value, void *userdata);
void pmm_ht_foreach(const CBMHashTable *ht, pmm_ht_iter_fn fn, void *userdata);

/* Clear all entries (keeps allocated memory). */
void pmm_ht_clear(CBMHashTable *ht);

#endif /* PMM_HASH_TABLE_H */
