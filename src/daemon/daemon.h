/*
 * daemon.h — Process-local coordination and wire framing for the PMM daemon.
 *
 * Transport and worker supervision live outside this module. The coordinator
 * binds clients and resource subscriptions to transport connections, coalesces
 * shared work, and defines the daemon's terminal shutdown transition.
 */
#ifndef PMM_DAEMON_H
#define PMM_DAEMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Permanent framing version for the account-wide rendezvous endpoint. Never
 * bump this for detailed runtime payload changes: incompatible executable
 * generations must still exchange the stable HELLO conflict envelope. */
#define PMM_DAEMON_RENDEZVOUS_FRAME_VERSION 1U
#define PMM_DAEMON_FRAME_HEADER_SIZE 12U
#define PMM_DAEMON_MAX_FRAME_SIZE (10U * 1024U * 1024U)
#define PMM_DAEMON_KEY_SIZE 17U

typedef enum {
    PMM_DAEMON_FRAME_REQUEST = 1,
    PMM_DAEMON_FRAME_RESPONSE = 2,
} pmm_daemon_frame_type_t;

typedef struct {
    pmm_daemon_frame_type_t type;
    uint16_t flags;
    uint32_t length;
} pmm_daemon_frame_t;

typedef struct pmm_daemon_coordinator pmm_daemon_coordinator_t;

typedef uint64_t pmm_daemon_client_id_t;
typedef uint64_t pmm_daemon_subscription_id_t;

#define PMM_DAEMON_CLIENT_ID_INVALID ((pmm_daemon_client_id_t)0)
#define PMM_DAEMON_SUBSCRIPTION_ID_INVALID ((pmm_daemon_subscription_id_t)0)

typedef enum {
    PMM_DAEMON_COORDINATOR_RUNNING = 1,
    PMM_DAEMON_COORDINATOR_STOPPING = 2,
} pmm_daemon_coordinator_state_t;

typedef enum {
    PMM_DAEMON_SUBSCRIPTION_REJECTED = 0,
    PMM_DAEMON_SUBSCRIPTION_STARTED = 1,
    PMM_DAEMON_SUBSCRIPTION_JOINED = 2,
} pmm_daemon_subscription_result_t;

typedef enum {
    PMM_DAEMON_JOB_NONE = 0,
    PMM_DAEMON_JOB_RUNNING = 1,
    PMM_DAEMON_JOB_CANCEL_REQUESTED = 2,
    PMM_DAEMON_JOB_REAPING = 3,
} pmm_daemon_job_state_t;

typedef void (*pmm_daemon_job_cancel_fn)(const char *project_key, void *context);
typedef void (*pmm_daemon_watch_release_fn)(const char *project_key, void *context);

typedef struct {
    pmm_daemon_job_cancel_fn cancel_job;
    pmm_daemon_watch_release_fn release_watch;
    void *context;
} pmm_daemon_coordinator_hooks_t;

/* lease_timeout_ms is fixed for the coordinator lifetime. All timestamps must
 * come from the same monotonic clock domain. */
pmm_daemon_coordinator_t *pmm_daemon_coordinator_new(uint64_t lease_timeout_ms);

/* A PERMANENT coordinator (backing a `daemon start` generation) never
 * self-transitions to STOPPING when its client count reaches zero; only the
 * explicit stop/drain paths end it. */
void pmm_daemon_coordinator_set_permanent(pmm_daemon_coordinator_t *coordinator, bool permanent);
/* The caller must first quiesce coordinator calls and hook invocations. */
void pmm_daemon_coordinator_free(pmm_daemon_coordinator_t *coordinator);

/* Hooks are copied. Their context must remain valid until the coordinator is
 * quiescent. Hooks are always invoked after releasing the coordinator mutex. */
bool pmm_daemon_coordinator_set_hooks(pmm_daemon_coordinator_t *coordinator,
                                      const pmm_daemon_coordinator_hooks_t *hooks);
pmm_daemon_coordinator_state_t pmm_daemon_coordinator_state(pmm_daemon_coordinator_t *coordinator);

/* Client IDs are daemon-issued, nonzero, monotonic, and never recycled. */
pmm_daemon_client_id_t pmm_daemon_client_connected(pmm_daemon_coordinator_t *coordinator,
                                                   uint64_t now_ms);
bool pmm_daemon_client_disconnected(pmm_daemon_coordinator_t *coordinator,
                                    pmm_daemon_client_id_t client_id, uint64_t now_ms);
bool pmm_daemon_client_heartbeat(pmm_daemon_coordinator_t *coordinator,
                                 pmm_daemon_client_id_t client_id, uint64_t now_ms);
size_t pmm_daemon_expire_leases(pmm_daemon_coordinator_t *coordinator, uint64_t now_ms);
size_t pmm_daemon_active_clients(pmm_daemon_coordinator_t *coordinator);

/* Every accepted subscription receives a unique daemon-issued handle. The
 * first subscriber starts the physical resource; later subscribers join it. */
pmm_daemon_subscription_result_t pmm_daemon_job_subscribe(
    pmm_daemon_coordinator_t *coordinator, pmm_daemon_client_id_t client_id,
    const char *project_key, pmm_daemon_subscription_id_t *subscription_id);
pmm_daemon_subscription_result_t pmm_daemon_watch_subscribe(
    pmm_daemon_coordinator_t *coordinator, pmm_daemon_client_id_t client_id,
    const char *project_key, pmm_daemon_subscription_id_t *subscription_id);
bool pmm_daemon_job_unsubscribe(pmm_daemon_coordinator_t *coordinator,
                                pmm_daemon_client_id_t client_id,
                                pmm_daemon_subscription_id_t subscription_id);
bool pmm_daemon_watch_unsubscribe(pmm_daemon_coordinator_t *coordinator,
                                  pmm_daemon_client_id_t client_id,
                                  pmm_daemon_subscription_id_t subscription_id);

size_t pmm_daemon_job_subscribers(pmm_daemon_coordinator_t *coordinator, const char *project_key);
size_t pmm_daemon_watch_subscribers(pmm_daemon_coordinator_t *coordinator, const char *project_key);
size_t pmm_daemon_active_jobs(pmm_daemon_coordinator_t *coordinator);
size_t pmm_daemon_active_watches(pmm_daemon_coordinator_t *coordinator);
pmm_daemon_job_state_t pmm_daemon_job_state(pmm_daemon_coordinator_t *coordinator,
                                            const char *project_key);

/* Cancellation is two phase. Losing the final subscriber requests cancel;
 * the job remains active until its supervisor reports completion/reaping. */
bool pmm_daemon_job_reaping(pmm_daemon_coordinator_t *coordinator, const char *project_key);
bool pmm_daemon_job_reaped(pmm_daemon_coordinator_t *coordinator, const char *project_key,
                           uint64_t now_ms);
bool pmm_daemon_job_completed(pmm_daemon_coordinator_t *coordinator, const char *project_key,
                              uint64_t now_ms);

/* STOPPING is terminal. Exit is ready only after every job/watch is gone. */
bool pmm_daemon_should_exit(pmm_daemon_coordinator_t *coordinator, uint64_t now_ms);

/* Encode/decode the permanently stable 12-byte "CBMD" rendezvous frame header
 * in network byte order. Detailed operation ABIs live above this framing. */
bool pmm_daemon_frame_header_encode(uint8_t header[PMM_DAEMON_FRAME_HEADER_SIZE],
                                    pmm_daemon_frame_type_t type, uint16_t flags, uint32_t length);
bool pmm_daemon_frame_header_decode(const uint8_t header[PMM_DAEMON_FRAME_HEADER_SIZE],
                                    pmm_daemon_frame_t *frame);

#endif /* PMM_DAEMON_H */
