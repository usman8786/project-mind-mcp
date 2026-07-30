/*
 * hash_table.c — CBMHashTable backed by Verstable.
 *
 * Public API in hash_table.h is unchanged. Internals are a Verstable
 * template instantiation (const char* → void*). Verstable is a 2024
 * open-addressing hash table using quadratic probing with metadata
 * stored separately from buckets (4-bit hash fragment + 11-bit
 * displacement + 1-bit in-home-bucket flag per uint16_t). Documented
 * in vendored/verstable/verstable.h.
 *
 * Why swap the prior Robin Hood implementation: cumulative profiling
 * showed pmm_ht_get is a hot path in resolve_file_calls's per-call
 * registry resolution. Verstable's 4-bit hash-fragment metadata
 * sidesteps most key comparisons during chain walks, which the prior
 * implementation could not.
 *
 * Lifetime: keys are BORROWED pointers (caller owns the strings).
 * Verstable's KEY_TY is const char*; the templated comparison +
 * hash use the standard vt_cmpr_string / vt_hash_string helpers.
 */
#include "foundation/constants.h"
#include "hash_table.h"
#include <stdlib.h>
#include <string.h>

/* Instantiate a Verstable map of (const char* → void*). The single
 * include below generates static inline functions named pmm_vt_init,
 * pmm_vt_cleanup, pmm_vt_get, pmm_vt_insert, etc., plus the pmm_vt
 * struct itself. */
#define NAME pmm_vt
#define KEY_TY const char *
#define VAL_TY void *
#define HASH_FN vt_hash_string
#define CMPR_FN vt_cmpr_string
#include "../../internal/pmm/vendored/verstable/verstable.h"

/* The opaque CBMHashTable struct holds the Verstable instance + a
 * count cache (Verstable's _size traversal is O(buckets) so we keep
 * our own atomic-free counter). */
struct CBMHashTable {
    pmm_vt vt;
};

CBMHashTable *pmm_ht_create(uint32_t initial_capacity) {
    CBMHashTable *ht = (CBMHashTable *)calloc(PMM_ALLOC_ONE, sizeof(*ht));
    if (!ht)
        return NULL;
    pmm_vt_init(&ht->vt);
    if (initial_capacity > 0) {
        /* Reserve enough buckets for the requested entries. Verstable
         * computes the minimum bucket count internally. */
        if (!pmm_vt_reserve(&ht->vt, (size_t)initial_capacity)) {
            pmm_vt_cleanup(&ht->vt);
            free(ht);
            return NULL;
        }
    }
    return ht;
}

void pmm_ht_free(CBMHashTable *ht) {
    if (!ht)
        return;
    pmm_vt_cleanup(&ht->vt);
    free(ht);
}

void *pmm_ht_set(CBMHashTable *ht, const char *key, void *value) {
    if (!ht || !key)
        return NULL;
    /* Capture previous value (if any) before overwriting.
     * Verstable's _insert overwrites silently and returns an iterator
     * to the (now updated) entry — we have to peek first to surface
     * the prior value to the caller (back-compat with our API). */
    void *prev = NULL;
    pmm_vt_itr itr = pmm_vt_get(&ht->vt, key);
    if (!pmm_vt_is_end(itr)) {
        prev = itr.data->val;
    }
    (void)pmm_vt_insert(&ht->vt, key, value);
    return prev;
}

void *pmm_ht_get(const CBMHashTable *ht, const char *key) {
    if (!ht || !key)
        return NULL;
    pmm_vt_itr itr = pmm_vt_get(&ht->vt, key);
    if (pmm_vt_is_end(itr))
        return NULL;
    return itr.data->val;
}

bool pmm_ht_has(const CBMHashTable *ht, const char *key) {
    if (!ht || !key)
        return false;
    pmm_vt_itr itr = pmm_vt_get(&ht->vt, key);
    return !pmm_vt_is_end(itr);
}

const char *pmm_ht_get_key(const CBMHashTable *ht, const char *key) {
    if (!ht || !key)
        return NULL;
    pmm_vt_itr itr = pmm_vt_get(&ht->vt, key);
    if (pmm_vt_is_end(itr))
        return NULL;
    return itr.data->key;
}

void *pmm_ht_delete(CBMHashTable *ht, const char *key) {
    if (!ht || !key)
        return NULL;
    pmm_vt_itr itr = pmm_vt_get(&ht->vt, key);
    if (pmm_vt_is_end(itr))
        return NULL;
    void *prev = itr.data->val;
    (void)pmm_vt_erase(&ht->vt, key);
    return prev;
}

uint32_t pmm_ht_count(const CBMHashTable *ht) {
    if (!ht)
        return 0;
    return (uint32_t)pmm_vt_size(&ht->vt);
}

void pmm_ht_foreach(const CBMHashTable *ht, pmm_ht_iter_fn fn, void *userdata) {
    if (!ht || !fn)
        return;
    for (pmm_vt_itr itr = pmm_vt_first(&ht->vt); !pmm_vt_is_end(itr); itr = pmm_vt_next(itr)) {
        fn(itr.data->key, itr.data->val, userdata);
    }
}

void pmm_ht_clear(CBMHashTable *ht) {
    if (!ht)
        return;
    pmm_vt_clear(&ht->vt);
}
