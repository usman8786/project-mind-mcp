/*
 * activation_transaction.h -- Transactional binary activation primitives.
 *
 * This is an internal CLI module.  It deliberately knows nothing about daemon
 * coordination or editor configuration: callers must acquire the maintenance
 * barrier before commit and retain it until finalize/rollback completes.
 */
#ifndef PMM_ACTIVATION_TRANSACTION_H
#define PMM_ACTIVATION_TRANSACTION_H

#include <stdbool.h>
#include <stddef.h>

typedef struct pmm_activation_transaction pmm_activation_transaction_t;

typedef enum {
    PMM_ACTIVATION_TRANSACTION_OK = 0,
    /* Windows could not unlink an inactive backup (normally because the old
     * executable image is still mapped) but safely registered it for deletion
     * at reboot.  The committed activation remains valid. */
    PMM_ACTIVATION_TRANSACTION_DEFERRED = 1,
    PMM_ACTIVATION_TRANSACTION_INVALID_ARGUMENT = -1,
    PMM_ACTIVATION_TRANSACTION_NO_MEMORY = -2,
    PMM_ACTIVATION_TRANSACTION_IO = -3,
    PMM_ACTIVATION_TRANSACTION_INVALID_STATE = -4,
    /* The post-commit validator rejected the candidate and rollback succeeded. */
    PMM_ACTIVATION_TRANSACTION_VALIDATION_FAILED = -5,
    /* The target changed, but restoring the retained backup also failed. */
    PMM_ACTIVATION_TRANSACTION_ROLLBACK_FAILED = -6,
} pmm_activation_transaction_status_t;

typedef bool (*pmm_activation_transaction_validator_fn)(const char *target_path, void *context);

/* Test-only seam: invoked after an absent target has been revalidated and
 * immediately before its staged candidate is published.  Production callers
 * leave this unset. */
typedef void (*pmm_activation_transaction_before_absent_publish_for_test_fn)(
    const char *target_path, void *context);
void pmm_activation_transaction_set_before_absent_publish_for_test(
    pmm_activation_transaction_before_absent_publish_for_test_fn hook, void *context);

/* Test-only seam: make the next `count` Windows rename attempts fail as though
 * another handle held the file, so the transient-lock retry can be proven
 * without racing a real scanner. Inert on POSIX and when count is 0. */
void pmm_activation_transaction_rename_failures_set_for_test(unsigned int count);

/* Stage a candidate beside target_path (therefore on the same filesystem).
 * The staged file is private to the current account and executable. */
pmm_activation_transaction_status_t pmm_activation_transaction_stage_bytes(
    const char *target_path, const void *candidate, size_t candidate_size,
    pmm_activation_transaction_t **transaction_out);

/* Copy candidate_path into a private executable stage beside target_path. */
pmm_activation_transaction_status_t pmm_activation_transaction_stage_file(
    const char *target_path, const char *candidate_path,
    pmm_activation_transaction_t **transaction_out);

/* Prepare an atomic removal.  A missing target is a valid no-op transaction. */
pmm_activation_transaction_status_t pmm_activation_transaction_stage_removal(
    const char *target_path, pmm_activation_transaction_t **transaction_out);

/* Atomically publish the candidate (or remove the target), retaining any old
 * target at backup_path.  If validator rejects the post-commit state, this
 * function rolls back before returning VALIDATION_FAILED. */
pmm_activation_transaction_status_t pmm_activation_transaction_commit(
    pmm_activation_transaction_t *transaction, pmm_activation_transaction_validator_fn validator,
    void *validator_context);

/* Restore the retained target after a successful commit. */
pmm_activation_transaction_status_t pmm_activation_transaction_rollback(
    pmm_activation_transaction_t *transaction);

/* Accept the committed state and delete the retained backup.  On Windows,
 * DEFERRED means deletion was safely registered for reboot; deferred_path
 * remains available for logging until close(). */
pmm_activation_transaction_status_t pmm_activation_transaction_finalize(
    pmm_activation_transaction_t *transaction);

/* Close an object.  An uncommitted object is cleanly aborted; a committed but
 * unfinalized object is rolled back.  On cleanup failure, ownership stays with
 * the caller so paths and rollback can be retried. */
pmm_activation_transaction_status_t pmm_activation_transaction_close(
    pmm_activation_transaction_t **transaction_io);

const char *pmm_activation_transaction_target_path(const pmm_activation_transaction_t *transaction);
const char *pmm_activation_transaction_staged_path(const pmm_activation_transaction_t *transaction);
const char *pmm_activation_transaction_backup_path(const pmm_activation_transaction_t *transaction);
const char *pmm_activation_transaction_deferred_path(
    const pmm_activation_transaction_t *transaction);

const char *pmm_activation_transaction_status_message(pmm_activation_transaction_status_t status);

/* Which security predicate refused the most recent transaction, as
 * "predicate (os N)", or "" when nothing refused since the last prepare.
 * The predicates refuse without a usable OS last-error, so this is the only
 * way a caller can say WHY staging failed. Reset by every prepare/stage
 * entry; single-threaded like the rest of the transaction API. */
const char *pmm_activation_transaction_refusal_note(void);

#endif /* PMM_ACTIVATION_TRANSACTION_H */
