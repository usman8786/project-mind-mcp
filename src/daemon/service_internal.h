/*
 * service_internal.h — Internal daemon service helpers and test hooks.
 *
 * Production callers outside the daemon implementation must use
 * daemon/service.h.  Runtime identity verification hashes an already-open,
 * kernel-bound file handle through this internal boundary; the test hook lets
 * concurrency tests establish an exact interleaving without scheduler luck.
 */
#ifndef PMM_DAEMON_SERVICE_INTERNAL_H
#define PMM_DAEMON_SERVICE_INTERNAL_H

#include "daemon/service.h"

#include <stdbool.h>
#include <stdint.h>

/* Hash an already-open regular file without closing it. native_file is an int
 * file descriptor on POSIX and a HANDLE cast through uintptr_t on Windows.
 * Callers use this after binding the handle to kernel process-image metadata,
 * avoiding a second lookup through a replaceable pathname. */
bool pmm_daemon_build_fingerprint_native_file(uintptr_t native_file,
                                              char out[PMM_DAEMON_BUILD_FINGERPRINT_SIZE]);

typedef enum {
    PMM_DAEMON_CONFLICT_LOG_BEFORE_SERIALIZATION_LOCK = 1,
    PMM_DAEMON_CONFLICT_LOG_AFTER_SERIALIZATION_LOCK,
} pmm_daemon_conflict_log_test_stage_t;

typedef void (*pmm_daemon_conflict_log_test_hook_fn)(void *context,
                                                     pmm_daemon_conflict_log_test_stage_t stage);

void pmm_daemon_conflict_log_set_test_hook(pmm_daemon_conflict_log_test_hook_fn hook,
                                           void *context);

#endif /* PMM_DAEMON_SERVICE_INTERNAL_H */
