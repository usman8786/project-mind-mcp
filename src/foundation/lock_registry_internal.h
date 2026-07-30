#ifndef PMM_LOCK_REGISTRY_INTERNAL_H
#define PMM_LOCK_REGISTRY_INTERNAL_H

#include "foundation/lock_registry.h"
#include "foundation/private_file_lock_internal.h"

typedef enum {
    PMM_LOCK_REGISTRY_STAGE_TURN_BUSY = 1,
    PMM_LOCK_REGISTRY_STAGE_TURN_HELD = 2,
    PMM_LOCK_REGISTRY_STAGE_RW_BUSY = 3,
    PMM_LOCK_REGISTRY_STAGE_NATIVE_READY = 4,
} pmm_lock_registry_stage_t;

typedef void (*pmm_lock_registry_stage_hook_fn)(void *context, pmm_private_file_lock_mode_t mode,
                                                pmm_lock_registry_stage_t stage);

bool pmm_lock_registry_resource_names(const char *resource_key,
                                      char turn_out[PMM_LOCK_REGISTRY_NAME_CAP],
                                      char rw_out[PMM_LOCK_REGISTRY_NAME_CAP]);

size_t pmm_lock_registry_waiter_count(pmm_lock_registry_t *registry);
size_t pmm_lock_registry_active_lease_count_for_test(pmm_lock_registry_t *registry);
size_t pmm_lock_registry_pending_cleanup_count_for_test(pmm_lock_registry_t *registry);
bool pmm_lock_registry_is_retired_for_test(const pmm_lock_registry_t *registry);
size_t pmm_lock_registry_attempting_waiter_count_for_test(pmm_lock_registry_t *registry);
uint64_t pmm_lock_registry_condition_wait_call_count_for_test(const pmm_lock_registry_t *registry);
size_t pmm_lock_registry_condition_waiter_count_for_test(const pmm_lock_registry_t *registry);

typedef enum {
    PMM_LOCK_REGISTRY_RELEASE_RW = 1,
    PMM_LOCK_REGISTRY_RELEASE_TURN = 2,
} pmm_lock_registry_release_handle_t;

bool pmm_lock_lease_fail_next_release_step_for_test(pmm_lock_lease_t *lease,
                                                    pmm_lock_registry_release_handle_t handle,
                                                    pmm_private_file_lock_release_step_t step);
bool pmm_lock_lease_has_release_handle_for_test(const pmm_lock_lease_t *lease,
                                                pmm_lock_registry_release_handle_t handle);
bool pmm_lock_lease_used_abort_lock_failure_path_for_test(const pmm_lock_lease_t *lease);
#ifndef _WIN32
bool pmm_lock_lease_fail_close_after_consuming_for_test(pmm_lock_lease_t *lease,
                                                        pmm_lock_registry_release_handle_t handle);
#endif
bool pmm_lock_registry_fail_next_native_release_step_for_test(
    pmm_lock_registry_t *registry, pmm_lock_registry_release_handle_t handle,
    pmm_private_file_lock_release_step_t step);

typedef enum {
    PMM_LOCK_REGISTRY_ABORT_FAIL_LOCK = 1,
    PMM_LOCK_REGISTRY_ABORT_FAIL_REMOVE = 2,
} pmm_lock_registry_abort_failure_t;

/* One-shot abort-bookkeeping fault seam, armable only while idle. */
bool pmm_lock_registry_fail_next_abort_bookkeeping_for_test(
    pmm_lock_registry_t *registry, pmm_lock_registry_abort_failure_t failure);

/* Deterministic test seam. May only be changed while the registry is idle;
 * callbacks run without the fork guard or registry mutex held. */
bool pmm_lock_registry_set_stage_hook_for_test(pmm_lock_registry_t *registry,
                                               pmm_lock_registry_stage_hook_fn hook, void *context);

#endif /* PMM_LOCK_REGISTRY_INTERNAL_H */
