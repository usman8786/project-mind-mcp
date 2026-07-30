/*
 * private_file_lock.h — Secure locks inside a prevalidated private directory.
 *
 * This is an internal foundation primitive. It deliberately does not choose a
 * product runtime path; callers must supply an opaque directory handle created
 * by the platform runtime-path layer.
 */
#ifndef PMM_PRIVATE_FILE_LOCK_H
#define PMM_PRIVATE_FILE_LOCK_H

#include <stdint.h>

typedef enum {
    PMM_PRIVATE_FILE_LOCK_OK = 0,
    PMM_PRIVATE_FILE_LOCK_BUSY = 1,
    PMM_PRIVATE_FILE_LOCK_UNSAFE = 2,
    PMM_PRIVATE_FILE_LOCK_IO = 3,
} pmm_private_file_lock_status_t;

typedef enum {
    PMM_PRIVATE_FILE_LOCK_SH = 1,
    PMM_PRIVATE_FILE_LOCK_EX = 2,
} pmm_private_file_lock_mode_t;

typedef struct pmm_private_lock_directory pmm_private_lock_directory_t;
typedef struct pmm_private_file_lock pmm_private_file_lock_t;

/* Basenames are fixed internal names, never paths. Acquisition is
 * nonblocking; BUSY is the only contention result. Stable lock files are never
 * unlinked by this API. Any non-NULL *lock_out on any status owns native
 * cleanup state and must be passed to pmm_private_file_lock_release(). */
pmm_private_file_lock_status_t pmm_private_file_lock_try_acquire(
    pmm_private_lock_directory_t *directory, const char *base_name,
    pmm_private_file_lock_mode_t mode, pmm_private_file_lock_t **lock_out);

/* OK terminally closes the native handle and clears *lock_io. IO retains a
 * non-NULL object only while native ownership is safely retryable. POSIX
 * close(2) consumes descriptor ownership once invoked even if it reports an
 * error, so that terminal IO case clears *lock_io to prevent fd-reuse races. */
pmm_private_file_lock_status_t pmm_private_file_lock_release(pmm_private_file_lock_t **lock_io);

void pmm_private_lock_directory_close(pmm_private_lock_directory_t *directory);
const char *pmm_private_lock_directory_path(const pmm_private_lock_directory_t *directory);

#endif /* PMM_PRIVATE_FILE_LOCK_H */
