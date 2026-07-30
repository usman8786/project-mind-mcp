/*
 * arena.h — Bump allocator with block-based growth.
 *
 * All memory is freed at once via pmm_arena_destroy(). Individual frees are
 * not supported — this is by design for per-file extraction where all data
 * has the same lifetime.
 *
 * Restructured from internal/pmm/arena.h for the pure C rewrite.
 * New additions: pmm_arena_reset() for reuse without realloc.
 */
#ifndef PMM_ARENA_H
#define PMM_ARENA_H

#include <stddef.h>
#include <stdarg.h>

#define PMM_ARENA_MAX_BLOCKS 256
#define PMM_ARENA_DEFAULT_BLOCK_SIZE ((size_t)64 * 1024) /* 64KB */

typedef struct {
    char *blocks[PMM_ARENA_MAX_BLOCKS];
    size_t block_sizes[PMM_ARENA_MAX_BLOCKS]; /* per-block sizes (for stats) */
    int nblocks;
    size_t block_size;  /* current block capacity */
    size_t used;        /* bytes used in current block */
    size_t total_alloc; /* cumulative bytes allocated (for stats) */
} CBMArena;

/* Initialize arena with default block size. */
void pmm_arena_init(CBMArena *a);

/* Initialize arena with a custom initial block size. */
void pmm_arena_init_sized(CBMArena *a, size_t block_size);

/* Allocate n bytes (8-byte aligned). Returns NULL on OOM. */
void *pmm_arena_alloc(CBMArena *a, size_t n);

/* Allocate n bytes, zero-initialized. */
void *pmm_arena_calloc(CBMArena *a, size_t n);

/* Duplicate a NUL-terminated string. */
char *pmm_arena_strdup(CBMArena *a, const char *s);

/* Duplicate a string of known length, NUL-terminate. */
char *pmm_arena_strndup(CBMArena *a, const char *s, size_t len);

/* sprintf into arena memory. */
char *pmm_arena_sprintf(CBMArena *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Reset arena for reuse: keeps first block, frees the rest. */
void pmm_arena_reset(CBMArena *a);

/* Free all blocks. Arena is zeroed after this. */
void pmm_arena_destroy(CBMArena *a);

/* Return total bytes allocated (for diagnostics). */
size_t pmm_arena_total(const CBMArena *a);

#endif /* PMM_ARENA_H */
