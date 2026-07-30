/* project_lock.h — Shared daemon/local-CLI project mutation leases. */
#ifndef PMM_DAEMON_PROJECT_LOCK_H
#define PMM_DAEMON_PROJECT_LOCK_H

#include "daemon/ipc.h"
#include "foundation/lock_registry.h"

#include <stdint.h>

typedef struct pmm_project_lock_manager pmm_project_lock_manager_t;
typedef struct pmm_project_lock_lease pmm_project_lock_lease_t;

/* Each manager is an independent process-local registry over the endpoint's
 * owner-only runtime directory. Separate PMM processes therefore coordinate
 * through the same native lock files without sharing memory. */
pmm_project_lock_manager_t *pmm_project_lock_manager_new(const pmm_daemon_ipc_endpoint_t *endpoint);

/* Normal projects hold SH(project-set) + EX(project). "*" holds
 * EX(project-set), blocking every named project. Project lock keys are ASCII
 * case-folded to cover filename aliases on case-insensitive filesystems. */
pmm_private_file_lock_status_t pmm_project_lock_acquire(pmm_project_lock_manager_t *manager,
                                                        const char *project, uint64_t deadline_ms,
                                                        const pmm_lock_cancel_token_t *cancel_token,
                                                        pmm_project_lock_lease_t **lease_out);

/* One fair, nonblocking attempt for UI/watcher paths. */
pmm_private_file_lock_status_t pmm_project_lock_try_acquire(pmm_project_lock_manager_t *manager,
                                                            const char *project,
                                                            pmm_project_lock_lease_t **lease_out);

pmm_private_file_lock_status_t pmm_project_lock_lease_release(pmm_project_lock_lease_t **lease_io);

pmm_private_file_lock_status_t pmm_project_lock_request_cancel(pmm_project_lock_manager_t *manager,
                                                               pmm_lock_cancel_token_t *token);

/* Refuses teardown while any lease/cleanup state remains. */
pmm_private_file_lock_status_t pmm_project_lock_manager_free(
    pmm_project_lock_manager_t **manager_io);

#endif /* PMM_DAEMON_PROJECT_LOCK_H */
