/*
 * application.h — Daemon-owned PMM application sessions and thin-client wire.
 *
 * The runtime authenticates a local process and owns connection lifetime. This
 * layer owns everything above that boundary: one isolated MCP session per
 * connection, explicit workspace context, shared watcher subscriptions,
 * daemon-owned index jobs, and one update-check generation. Frontends never
 * construct stores or watchers.
 */
#ifndef PMM_DAEMON_APPLICATION_H
#define PMM_DAEMON_APPLICATION_H

#include "daemon/runtime.h"
#include "daemon/project_lock.h"
#include "mcp/index_supervisor.h"
#include "mcp/mcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pmm_config;
struct pmm_watcher;

typedef struct pmm_daemon_application pmm_daemon_application_t;
typedef void *pmm_daemon_application_worker_t;
typedef void *pmm_daemon_application_update_worker_t;

/* Injectable physical-worker boundary. Production uses index_supervisor;
 * tests use this to deterministically hold/release one shared job. */
typedef struct {
    void *context;
    int (*start)(void *context, const char *args_json, size_t memory_budget_bytes,
                 const char *marker_file, const char *quarantine_file,
                 pmm_daemon_application_worker_t *worker_out);
    pmm_index_worker_poll_t (*poll)(void *context, pmm_daemon_application_worker_t worker,
                                    const pmm_index_worker_result_t **result_out);
    bool (*cancel)(void *context, pmm_daemon_application_worker_t worker);
    const char *(*log_path)(void *context, pmm_daemon_application_worker_t worker);
    void (*destroy)(void *context, pmm_daemon_application_worker_t worker);
} pmm_daemon_application_worker_ops_t;

typedef enum {
    PMM_DAEMON_APPLICATION_UPDATE_POLL_ERROR = -1,
    PMM_DAEMON_APPLICATION_UPDATE_POLL_RUNNING = 0,
    PMM_DAEMON_APPLICATION_UPDATE_POLL_TERMINAL = 1,
} pmm_daemon_application_update_poll_t;

/* Injectable update-check boundary. Production supervises one curl process;
 * tests provide an offline worker. latest_version_out is borrowed from the
 * worker and is only meaningful for a clean terminal result. RUNNING is the
 * only non-terminal poll result; ERROR is a contained terminal failure. cancel
 * must be safe concurrently with poll, and destroy receives only a terminal
 * worker. */
typedef struct {
    void *context;
    int (*start)(void *context, pmm_daemon_application_update_worker_t *worker_out);
    pmm_daemon_application_update_poll_t (*poll)(void *context,
                                                 pmm_daemon_application_update_worker_t worker,
                                                 const char **latest_version_out);
    bool (*cancel)(void *context, pmm_daemon_application_update_worker_t worker);
    void (*destroy)(void *context, pmm_daemon_application_update_worker_t worker);
} pmm_daemon_application_update_ops_t;

typedef struct {
    struct pmm_watcher *watcher;                           /* borrowed; daemon lifetime */
    struct pmm_config *config;                             /* borrowed; daemon lifetime */
    const pmm_daemon_application_worker_ops_t *worker_ops; /* NULL = production */
    const pmm_daemon_application_update_ops_t *update_ops; /* NULL = production */
    /* Maximum distinct, non-terminal physical index jobs. Zero selects the
     * conservative daemon default (4). Identical requests still coalesce even
     * while the limit is full. */
    size_t physical_job_limit;
    /* Aggregate budget shared by every concurrently admitted physical worker.
     * The daemon host supplies its already-resolved PMM process budget. */
    size_t aggregate_memory_budget_bytes;
    /* Borrowed native-lock manager for daemon-owned mutations (MCP tools,
     * watcher/UI operations). Index workers create and own their independent
     * process-local manager; the daemon job registry reserves index projects
     * in-process and must not hold the worker's OS lease. */
    pmm_project_lock_manager_t *project_locks;
} pmm_daemon_application_config_t;

typedef enum {
    PMM_DAEMON_APPLICATION_REQUEST_SET_CONTEXT = 1,
    PMM_DAEMON_APPLICATION_REQUEST_MCP = 2,
    PMM_DAEMON_APPLICATION_REQUEST_TOOL = 3,
    PMM_DAEMON_APPLICATION_REQUEST_HOOK_AUGMENT = 4,
    PMM_DAEMON_APPLICATION_REQUEST_SET_UI_CONFIG = 5,
} pmm_daemon_application_request_kind_t;

typedef enum {
    PMM_DAEMON_APPLICATION_UI_CONFIG_ENABLED = 1U << 0,
    PMM_DAEMON_APPLICATION_UI_CONFIG_PORT = 1U << 1,
} pmm_daemon_application_ui_config_mask_t;

pmm_daemon_application_t *pmm_daemon_application_new(const pmm_daemon_application_config_t *config);

/* A PERMANENT application layer (backing a `daemon start` generation) keeps
 * admitting new sessions after its last live session ends; only the explicit
 * stop/drain paths latch it stopping. */
void pmm_daemon_application_set_permanent(pmm_daemon_application_t *application, bool permanent);

/* Cancel and reap all daemon-owned operations within timeout_ms. Normal final
 * client shutdown calls this before watcher/store teardown. Idempotent. */
bool pmm_daemon_application_shutdown(pmm_daemon_application_t *application, uint32_t timeout_ms);
/* Destroy application storage only after every borrowed callback and physical
 * operation is quiescent. Returns false without freeing anything when the
 * timeout expires; the caller must retain both the application and every
 * borrowed dependency, then retry or fail-stop the owning process. */
bool pmm_daemon_application_free_with_timeout(pmm_daemon_application_t *application,
                                              uint32_t timeout_ms);
bool pmm_daemon_application_free(pmm_daemon_application_t *application);

/* Callbacks are borrowed from application and remain valid until free. */
pmm_daemon_runtime_application_callbacks_t pmm_daemon_application_runtime_callbacks(
    pmm_daemon_application_t *application);

/* Thin-client request helpers. Every helper performs one bounded runtime
 * exchange. MCP notifications succeed with a NULL/zero response. Returned
 * response bytes are malloc-owned and include one trailing NUL for text use;
 * response_length excludes that terminator. */
pmm_daemon_runtime_application_status_t pmm_daemon_application_client_set_context(
    pmm_daemon_runtime_client_t *client, const char *session_root, const char *allowed_root,
    pmm_mcp_tool_profile_t tool_profile, const char *hook_event, const char *hook_dialect,
    uint32_t timeout_ms);

/* Persist a masked UI configuration mutation in the daemon. A zero/unknown
 * mask, an invalid port, or a non-canonical unused field is rejected before
 * transport. */
pmm_daemon_runtime_application_status_t pmm_daemon_application_client_set_ui_config(
    pmm_daemon_runtime_client_t *client, uint8_t update_mask, bool ui_enabled, int ui_port,
    uint32_t timeout_ms);

pmm_daemon_runtime_application_status_t pmm_daemon_application_client_mcp(
    pmm_daemon_runtime_client_t *client, const char *message, uint8_t **response_out,
    uint32_t *response_length_out, uint32_t timeout_ms);

/* Cancellable frontend variant using a token reserved on the runtime client. */
pmm_daemon_runtime_application_status_t pmm_daemon_application_client_mcp_tagged(
    pmm_daemon_runtime_client_t *client, pmm_daemon_runtime_application_token_t request_token,
    const char *message, uint8_t **response_out, uint32_t *response_length_out,
    uint32_t timeout_ms);

pmm_daemon_runtime_application_status_t pmm_daemon_application_client_tool(
    pmm_daemon_runtime_client_t *client, const char *tool_name, const char *args_json,
    uint8_t **response_out, uint32_t *response_length_out, uint32_t timeout_ms);

pmm_daemon_runtime_application_status_t pmm_daemon_application_client_hook_augment(
    pmm_daemon_runtime_client_t *client, const char *input_json, uint8_t **response_out,
    uint32_t *response_length_out, uint32_t timeout_ms);

/* Watcher callback: atomically validates project/root ownership and subscribes
 * the shared physical job to every exact live session watch. The callback is
 * only a result waiter, so disconnecting the final matching owner cancels the
 * job even while unrelated daemon sessions remain. Returns 0 on success,
 * positive when stale, cancelled, or busy (retry), and negative on a terminal
 * worker error. */
int pmm_daemon_application_watcher_index(const char *project_name, const char *root_path,
                                         void *context);

/* Shared daemon/UI entry point for a default full index operation. */
int pmm_daemon_application_index(pmm_daemon_application_t *application, const char *project_name,
                                 const char *root_path);

/* Non-blocking mutation lease for daemon-owned UI endpoints. A successful
 * begin must be paired with end. Returns false while the project (or global
 * project set "*") is reserved by indexing, another mutation, or shutdown. */
bool pmm_daemon_application_project_mutation_try_begin(pmm_daemon_application_t *application,
                                                       const char *project);
void pmm_daemon_application_project_mutation_end(pmm_daemon_application_t *application,
                                                 const char *project);

/* Read-only coordination metrics used by daemon diagnostics/tests. */
size_t pmm_daemon_application_active_jobs(pmm_daemon_application_t *application);
size_t pmm_daemon_application_job_subscribers(pmm_daemon_application_t *application,
                                              const char *project_key);
size_t pmm_daemon_application_physical_job_limit(pmm_daemon_application_t *application);
size_t pmm_daemon_application_worker_memory_budget_bytes(pmm_daemon_application_t *application);

#endif /* PMM_DAEMON_APPLICATION_H */
