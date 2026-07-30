/*
 * compat_thread.h — Portable threading: pthreads on POSIX, Win32 threads on Windows.
 *
 * Provides: thread create/join, mutex, aligned allocation.
 * All have zero overhead on POSIX (thin inlines or macros).
 */
#ifndef PMM_COMPAT_THREAD_H
#define PMM_COMPAT_THREAD_H

#include <stddef.h>

/* ── Thread ───────────────────────────────────────────────────── */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct {
    HANDLE handle;
} pmm_thread_t;

#else /* POSIX */

#include <pthread.h>

typedef struct {
    pthread_t handle;
} pmm_thread_t;

#endif

/* Create a thread with the given stack size (0 = OS default).
 * fn receives arg. Returns 0 on success. */
int pmm_thread_create(pmm_thread_t *t, size_t stack_size, void *(*fn)(void *), void *arg);

/* Wait for thread to finish. Returns 0 on success. */
int pmm_thread_join(pmm_thread_t *t);

/* Detach thread so resources are freed on exit. Returns 0 on success. */
int pmm_thread_detach(pmm_thread_t *t);

/* ── Mutex ────────────────────────────────────────────────────── */

#ifdef _WIN32

typedef struct {
    CRITICAL_SECTION cs;
} pmm_mutex_t;

#else

typedef struct {
    pthread_mutex_t mtx;
} pmm_mutex_t;

#endif

void pmm_mutex_init(pmm_mutex_t *m);
void pmm_mutex_lock(pmm_mutex_t *m);
void pmm_mutex_unlock(pmm_mutex_t *m);
void pmm_mutex_destroy(pmm_mutex_t *m);

/* ── Aligned allocation ───────────────────────────────────────── */

/* Allocate size bytes aligned to alignment boundary.
 * Returns 0 on success, non-zero on failure. *ptr receives the allocation. */
int pmm_aligned_alloc(void **ptr, size_t alignment, size_t size);

/* Free memory from pmm_aligned_alloc. */
void pmm_aligned_free(void *ptr);

#endif /* PMM_COMPAT_THREAD_H */
