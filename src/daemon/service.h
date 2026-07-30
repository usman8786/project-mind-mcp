/*
 * service.h — Stable daemon rendezvous and build-compatibility policy.
 *
 * The rendezvous key deliberately excludes executable path, release version,
 * build fingerprint, cache directory, and ABI values. Every stateful PMM
 * frontend for one OS account must meet at one endpoint; the HELLO comparison
 * then either admits the exact build or returns an explicit conflict.
 */
#ifndef PMM_DAEMON_SERVICE_H
#define PMM_DAEMON_SERVICE_H

#include "daemon/daemon.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PMM_DAEMON_VERSION_TEXT_SIZE 64U
#define PMM_DAEMON_BUILD_FINGERPRINT_SIZE 65U
#define PMM_DAEMON_CONFLICT_MESSAGE_SIZE 512U

typedef struct {
    const char *semantic_version;
    const char *build_fingerprint;
    /* SHA-256 of the canonical cache-root path. It is intentionally excluded
     * from the stable HELLO envelope, but the account-wide lifetime cohort
     * compares it before any daemon/CLI work can begin. NULL means an internal
     * test/legacy identity with no cache namespace. */
    const char *cache_fingerprint;
    uint32_t protocol_abi;
    uint32_t store_abi;
    uint32_t feature_abi;
} pmm_daemon_build_identity_t;

typedef enum {
    PMM_DAEMON_HELLO_INVALID = 0,
    PMM_DAEMON_HELLO_COMPATIBLE,
    PMM_DAEMON_HELLO_VERSION_CONFLICT,
    PMM_DAEMON_HELLO_BUILD_CONFLICT,
    PMM_DAEMON_HELLO_PROTOCOL_ABI_CONFLICT,
    PMM_DAEMON_HELLO_STORE_ABI_CONFLICT,
    PMM_DAEMON_HELLO_FEATURE_ABI_CONFLICT,
    PMM_DAEMON_HELLO_CACHE_CONFLICT,
} pmm_daemon_hello_status_t;

typedef struct {
    pmm_daemon_hello_status_t status;
    char active_version[PMM_DAEMON_VERSION_TEXT_SIZE];
    char active_build_fingerprint[PMM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char requested_version[PMM_DAEMON_VERSION_TEXT_SIZE];
    char requested_build_fingerprint[PMM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char active_cache_fingerprint[PMM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char requested_cache_fingerprint[PMM_DAEMON_BUILD_FINGERPRINT_SIZE];
} pmm_daemon_conflict_t;

/* Stable product key. OS-account isolation is supplied by the IPC runtime
 * directory / current-user ACL, never by caller-provided identity text. */
bool pmm_daemon_rendezvous_key(char out[PMM_DAEMON_KEY_SIZE]);

/* SHA-256 of the exact executable bytes, encoded as 64 lowercase hex
 * characters plus NUL. This is captured once at process startup. */
bool pmm_daemon_build_fingerprint_file(const char *path,
                                       char out[PMM_DAEMON_BUILD_FINGERPRINT_SIZE]);

pmm_daemon_hello_status_t pmm_daemon_hello_compare(const pmm_daemon_build_identity_t *active,
                                                   const pmm_daemon_build_identity_t *requested,
                                                   pmm_daemon_conflict_t *conflict_out);

bool pmm_daemon_conflict_format(const pmm_daemon_conflict_t *conflict, char *out, size_t out_size);

/* Append one secret-free NDJSON conflict event. A persistent owner-only
 * <log_path>.lock serializes validation, rotation, and append across daemon
 * processes; cap_bytes rotates one complete prior generation to <log_path>.1
 * before appending a record that would cross the cap. */
bool pmm_daemon_conflict_log_append(const char *log_path, const pmm_daemon_conflict_t *conflict,
                                    size_t cap_bytes);

#endif /* PMM_DAEMON_SERVICE_H */
