/*
 * daemon.c — Process-local coordination and wire framing for the PMM daemon.
 */
#include "daemon/daemon.h"

#include "foundation/compat_thread.h"

#include <stdlib.h>
#include <string.h>

typedef struct pmm_daemon_subscription {
    pmm_daemon_subscription_id_t id;
    pmm_daemon_client_id_t client_id;
    struct pmm_daemon_subscription *next;
} pmm_daemon_subscription_t;

typedef struct pmm_daemon_client {
    pmm_daemon_client_id_t id;
    uint64_t last_heartbeat_ms;
    struct pmm_daemon_client *next;
} pmm_daemon_client_t;

typedef struct pmm_daemon_job {
    char *project_key;
    pmm_daemon_job_state_t state;
    pmm_daemon_subscription_t *subscriptions;
    size_t subscription_count;
    bool cancel_callback_inflight;
    bool detached;
    struct pmm_daemon_job *next;
    struct pmm_daemon_job *action_next;
} pmm_daemon_job_t;

typedef struct pmm_daemon_watch {
    char *project_key;
    pmm_daemon_subscription_t *subscriptions;
    size_t subscription_count;
    struct pmm_daemon_watch *next;
    struct pmm_daemon_watch *action_next;
} pmm_daemon_watch_t;

struct pmm_daemon_coordinator {
    pmm_mutex_t mutex;
    pmm_daemon_client_t *clients;
    pmm_daemon_job_t *jobs;
    pmm_daemon_watch_t *watches;
    size_t client_count;
    /* See pmm_daemon_coordinator_set_permanent. */
    bool permanent;
    size_t job_count;
    size_t watch_count;
    size_t callback_count;
    uint64_t lease_timeout_ms;
    pmm_daemon_client_id_t last_client_id;
    pmm_daemon_subscription_id_t last_subscription_id;
    pmm_daemon_coordinator_state_t state;
    pmm_daemon_coordinator_hooks_t hooks;
};

typedef struct {
    pmm_daemon_job_t *jobs;
    pmm_daemon_watch_t *watches;
    pmm_daemon_job_cancel_fn cancel_job;
    pmm_daemon_watch_release_fn release_watch;
    void *context;
} pmm_daemon_callback_batch_t;

enum {
    FRAME_MAGIC_0 = 0,
    FRAME_MAGIC_1 = 1,
    FRAME_MAGIC_2 = 2,
    FRAME_MAGIC_3 = 3,
    FRAME_VERSION = 4,
    FRAME_TYPE = 5,
    FRAME_FLAGS_HI = 6,
    FRAME_FLAGS_LO = 7,
    FRAME_LENGTH_3 = 8,
    FRAME_LENGTH_2 = 9,
    FRAME_LENGTH_1 = 10,
    FRAME_LENGTH_0 = 11,
};

static bool frame_type_valid(pmm_daemon_frame_type_t type) {
    return type == PMM_DAEMON_FRAME_REQUEST || type == PMM_DAEMON_FRAME_RESPONSE;
}

static char *daemon_string_dup(const char *value) {
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static void free_subscriptions(pmm_daemon_subscription_t *subscription) {
    while (subscription) {
        pmm_daemon_subscription_t *next = subscription->next;
        free(subscription);
        subscription = next;
    }
}

static void free_job(pmm_daemon_job_t *job) {
    if (job) {
        free_subscriptions(job->subscriptions);
        free(job->project_key);
        free(job);
    }
}

static void free_watch(pmm_daemon_watch_t *watch) {
    if (watch) {
        free_subscriptions(watch->subscriptions);
        free(watch->project_key);
        free(watch);
    }
}

static pmm_daemon_client_id_t issue_client_id_locked(pmm_daemon_coordinator_t *coordinator) {
    if (coordinator->last_client_id == UINT64_MAX) {
        return PMM_DAEMON_CLIENT_ID_INVALID;
    }
    coordinator->last_client_id++;
    return coordinator->last_client_id;
}

static pmm_daemon_subscription_id_t issue_subscription_id_locked(
    pmm_daemon_coordinator_t *coordinator) {
    if (coordinator->last_subscription_id == UINT64_MAX) {
        return PMM_DAEMON_SUBSCRIPTION_ID_INVALID;
    }
    coordinator->last_subscription_id++;
    return coordinator->last_subscription_id;
}

static pmm_daemon_client_t *find_client_locked(pmm_daemon_coordinator_t *coordinator,
                                               pmm_daemon_client_id_t client_id) {
    for (pmm_daemon_client_t *client = coordinator->clients; client; client = client->next) {
        if (client->id == client_id) {
            return client;
        }
    }
    return NULL;
}

static pmm_daemon_job_t *find_job_locked(pmm_daemon_coordinator_t *coordinator,
                                         const char *project_key) {
    for (pmm_daemon_job_t *job = coordinator->jobs; job; job = job->next) {
        if (strcmp(job->project_key, project_key) == 0) {
            return job;
        }
    }
    return NULL;
}

static pmm_daemon_watch_t *find_watch_locked(pmm_daemon_coordinator_t *coordinator,
                                             const char *project_key) {
    for (pmm_daemon_watch_t *watch = coordinator->watches; watch; watch = watch->next) {
        if (strcmp(watch->project_key, project_key) == 0) {
            return watch;
        }
    }
    return NULL;
}

static bool remove_subscription_locked(pmm_daemon_subscription_t **subscriptions,
                                       size_t *subscription_count, pmm_daemon_client_id_t client_id,
                                       pmm_daemon_subscription_id_t subscription_id) {
    pmm_daemon_subscription_t **cursor = subscriptions;
    while (*cursor) {
        pmm_daemon_subscription_t *subscription = *cursor;
        if (subscription->id == subscription_id && subscription->client_id == client_id) {
            *cursor = subscription->next;
            free(subscription);
            (*subscription_count)--;
            return true;
        }
        cursor = &subscription->next;
    }
    return false;
}

static void remove_client_subscriptions_locked(pmm_daemon_subscription_t **subscriptions,
                                               size_t *subscription_count,
                                               pmm_daemon_client_id_t client_id) {
    pmm_daemon_subscription_t **cursor = subscriptions;
    while (*cursor) {
        pmm_daemon_subscription_t *subscription = *cursor;
        if (subscription->client_id == client_id) {
            *cursor = subscription->next;
            free(subscription);
            (*subscription_count)--;
        } else {
            cursor = &subscription->next;
        }
    }
}

static void callback_batch_init_locked(pmm_daemon_coordinator_t *coordinator,
                                       pmm_daemon_callback_batch_t *batch) {
    memset(batch, 0, sizeof(*batch));
    batch->cancel_job = coordinator->hooks.cancel_job;
    batch->release_watch = coordinator->hooks.release_watch;
    batch->context = coordinator->hooks.context;
}

static void request_job_cancel_locked(pmm_daemon_coordinator_t *coordinator, pmm_daemon_job_t *job,
                                      pmm_daemon_callback_batch_t *batch) {
    if (job->subscription_count != 0 || job->state != PMM_DAEMON_JOB_RUNNING) {
        return;
    }
    job->state = PMM_DAEMON_JOB_CANCEL_REQUESTED;
    if (batch->cancel_job) {
        job->cancel_callback_inflight = true;
        job->action_next = batch->jobs;
        batch->jobs = job;
        coordinator->callback_count++;
    }
}

static void queue_watch_release_locked(pmm_daemon_coordinator_t *coordinator,
                                       pmm_daemon_watch_t *watch,
                                       pmm_daemon_callback_batch_t *batch) {
    watch->action_next = batch->watches;
    batch->watches = watch;
    if (batch->release_watch) {
        coordinator->callback_count++;
    }
}

static void callback_batch_run(pmm_daemon_coordinator_t *coordinator,
                               pmm_daemon_callback_batch_t *batch) {
    pmm_daemon_job_t *job = batch->jobs;
    while (job) {
        pmm_daemon_job_t *next = job->action_next;
        batch->cancel_job(job->project_key, batch->context);

        pmm_mutex_lock(&coordinator->mutex);
        coordinator->callback_count--;
        job->cancel_callback_inflight = false;
        bool detached = job->detached;
        pmm_mutex_unlock(&coordinator->mutex);
        if (detached) {
            free_job(job);
        }
        job = next;
    }

    pmm_daemon_watch_t *watch = batch->watches;
    while (watch) {
        pmm_daemon_watch_t *next = watch->action_next;
        if (batch->release_watch) {
            batch->release_watch(watch->project_key, batch->context);
            pmm_mutex_lock(&coordinator->mutex);
            coordinator->callback_count--;
            pmm_mutex_unlock(&coordinator->mutex);
        }
        free_watch(watch);
        watch = next;
    }
}

static void release_client_resources_locked(pmm_daemon_coordinator_t *coordinator,
                                            pmm_daemon_client_id_t client_id,
                                            pmm_daemon_callback_batch_t *batch) {
    for (pmm_daemon_job_t *job = coordinator->jobs; job; job = job->next) {
        remove_client_subscriptions_locked(&job->subscriptions, &job->subscription_count,
                                           client_id);
        request_job_cancel_locked(coordinator, job, batch);
    }

    pmm_daemon_watch_t **watch_cursor = &coordinator->watches;
    while (*watch_cursor) {
        pmm_daemon_watch_t *watch = *watch_cursor;
        remove_client_subscriptions_locked(&watch->subscriptions, &watch->subscription_count,
                                           client_id);
        if (watch->subscription_count == 0) {
            *watch_cursor = watch->next;
            watch->next = NULL;
            coordinator->watch_count--;
            queue_watch_release_locked(coordinator, watch, batch);
        } else {
            watch_cursor = &watch->next;
        }
    }
}

static void release_client_locked(pmm_daemon_coordinator_t *coordinator,
                                  pmm_daemon_client_t *client, pmm_daemon_callback_batch_t *batch) {
    release_client_resources_locked(coordinator, client->id, batch);
    free(client);
    coordinator->client_count--;
    if (coordinator->client_count == 0 && !coordinator->permanent) {
        coordinator->state = PMM_DAEMON_COORDINATOR_STOPPING;
    }
}

static bool terminal_job_locked(pmm_daemon_coordinator_t *coordinator, const char *project_key,
                                bool require_cancellation, pmm_daemon_job_t **free_after_unlock) {
    pmm_daemon_job_t **cursor = &coordinator->jobs;
    while (*cursor && strcmp((*cursor)->project_key, project_key) != 0) {
        cursor = &(*cursor)->next;
    }
    if (!*cursor || (require_cancellation && (*cursor)->state == PMM_DAEMON_JOB_RUNNING)) {
        return false;
    }

    pmm_daemon_job_t *job = *cursor;
    *cursor = job->next;
    job->next = NULL;
    job->detached = true;
    coordinator->job_count--;
    free_subscriptions(job->subscriptions);
    job->subscriptions = NULL;
    job->subscription_count = 0;
    if (!job->cancel_callback_inflight) {
        *free_after_unlock = job;
    }
    return true;
}

void pmm_daemon_coordinator_set_permanent(pmm_daemon_coordinator_t *coordinator, bool permanent) {
    if (!coordinator) {
        return;
    }
    pmm_mutex_lock(&coordinator->mutex);
    coordinator->permanent = permanent;
    pmm_mutex_unlock(&coordinator->mutex);
}

pmm_daemon_coordinator_t *pmm_daemon_coordinator_new(uint64_t lease_timeout_ms) {
    pmm_daemon_coordinator_t *coordinator = calloc(1, sizeof(*coordinator));
    if (!coordinator) {
        return NULL;
    }
    pmm_mutex_init(&coordinator->mutex);
    coordinator->lease_timeout_ms = lease_timeout_ms;
    coordinator->state = PMM_DAEMON_COORDINATOR_RUNNING;
    return coordinator;
}

void pmm_daemon_coordinator_free(pmm_daemon_coordinator_t *coordinator) {
    if (!coordinator) {
        return;
    }

    pmm_daemon_client_t *client = coordinator->clients;
    while (client) {
        pmm_daemon_client_t *next = client->next;
        free(client);
        client = next;
    }

    pmm_daemon_job_t *job = coordinator->jobs;
    while (job) {
        pmm_daemon_job_t *next = job->next;
        free_job(job);
        job = next;
    }

    pmm_daemon_watch_t *watch = coordinator->watches;
    while (watch) {
        pmm_daemon_watch_t *next = watch->next;
        free_watch(watch);
        watch = next;
    }
    pmm_mutex_destroy(&coordinator->mutex);
    free(coordinator);
}

bool pmm_daemon_coordinator_set_hooks(pmm_daemon_coordinator_t *coordinator,
                                      const pmm_daemon_coordinator_hooks_t *hooks) {
    if (!coordinator || !hooks) {
        return false;
    }
    pmm_mutex_lock(&coordinator->mutex);
    coordinator->hooks = *hooks;
    pmm_mutex_unlock(&coordinator->mutex);
    return true;
}

pmm_daemon_coordinator_state_t pmm_daemon_coordinator_state(pmm_daemon_coordinator_t *coordinator) {
    if (!coordinator) {
        return PMM_DAEMON_COORDINATOR_STOPPING;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_coordinator_state_t state = coordinator->state;
    pmm_mutex_unlock(&coordinator->mutex);
    return state;
}

pmm_daemon_client_id_t pmm_daemon_client_connected(pmm_daemon_coordinator_t *coordinator,
                                                   uint64_t now_ms) {
    if (!coordinator) {
        return PMM_DAEMON_CLIENT_ID_INVALID;
    }

    pmm_daemon_client_t *client = malloc(sizeof(*client));
    if (!client) {
        return PMM_DAEMON_CLIENT_ID_INVALID;
    }

    pmm_mutex_lock(&coordinator->mutex);
    if (coordinator->state != PMM_DAEMON_COORDINATOR_RUNNING) {
        pmm_mutex_unlock(&coordinator->mutex);
        free(client);
        return PMM_DAEMON_CLIENT_ID_INVALID;
    }
    pmm_daemon_client_id_t client_id = issue_client_id_locked(coordinator);
    if (client_id == PMM_DAEMON_CLIENT_ID_INVALID) {
        pmm_mutex_unlock(&coordinator->mutex);
        free(client);
        return PMM_DAEMON_CLIENT_ID_INVALID;
    }
    client->id = client_id;
    client->last_heartbeat_ms = now_ms;
    client->next = coordinator->clients;
    coordinator->clients = client;
    coordinator->client_count++;
    pmm_mutex_unlock(&coordinator->mutex);
    return client_id;
}

bool pmm_daemon_client_disconnected(pmm_daemon_coordinator_t *coordinator,
                                    pmm_daemon_client_id_t client_id, uint64_t now_ms) {
    (void)now_ms;
    if (!coordinator || client_id == PMM_DAEMON_CLIENT_ID_INVALID) {
        return false;
    }

    pmm_daemon_callback_batch_t batch;
    pmm_mutex_lock(&coordinator->mutex);
    callback_batch_init_locked(coordinator, &batch);
    pmm_daemon_client_t **cursor = &coordinator->clients;
    while (*cursor && (*cursor)->id != client_id) {
        cursor = &(*cursor)->next;
    }
    if (!*cursor) {
        pmm_mutex_unlock(&coordinator->mutex);
        return false;
    }
    pmm_daemon_client_t *client = *cursor;
    *cursor = client->next;
    release_client_locked(coordinator, client, &batch);
    pmm_mutex_unlock(&coordinator->mutex);

    callback_batch_run(coordinator, &batch);
    return true;
}

bool pmm_daemon_client_heartbeat(pmm_daemon_coordinator_t *coordinator,
                                 pmm_daemon_client_id_t client_id, uint64_t now_ms) {
    if (!coordinator || client_id == PMM_DAEMON_CLIENT_ID_INVALID) {
        return false;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_client_t *client = find_client_locked(coordinator, client_id);
    bool found = client != NULL;
    if (client && now_ms > client->last_heartbeat_ms) {
        client->last_heartbeat_ms = now_ms;
    }
    pmm_mutex_unlock(&coordinator->mutex);
    return found;
}

size_t pmm_daemon_expire_leases(pmm_daemon_coordinator_t *coordinator, uint64_t now_ms) {
    if (!coordinator) {
        return 0;
    }

    size_t expired_count = 0;
    pmm_daemon_callback_batch_t batch;
    pmm_mutex_lock(&coordinator->mutex);
    callback_batch_init_locked(coordinator, &batch);
    pmm_daemon_client_t **cursor = &coordinator->clients;
    while (*cursor) {
        pmm_daemon_client_t *client = *cursor;
        bool expired = now_ms >= client->last_heartbeat_ms &&
                       now_ms - client->last_heartbeat_ms >= coordinator->lease_timeout_ms;
        if (!expired) {
            cursor = &client->next;
            continue;
        }
        *cursor = client->next;
        release_client_locked(coordinator, client, &batch);
        expired_count++;
    }
    pmm_mutex_unlock(&coordinator->mutex);

    callback_batch_run(coordinator, &batch);
    return expired_count;
}

size_t pmm_daemon_active_clients(pmm_daemon_coordinator_t *coordinator) {
    if (!coordinator) {
        return 0;
    }
    pmm_mutex_lock(&coordinator->mutex);
    size_t count = coordinator->client_count;
    pmm_mutex_unlock(&coordinator->mutex);
    return count;
}

pmm_daemon_subscription_result_t pmm_daemon_job_subscribe(
    pmm_daemon_coordinator_t *coordinator, pmm_daemon_client_id_t client_id,
    const char *project_key, pmm_daemon_subscription_id_t *subscription_id) {
    if (subscription_id) {
        *subscription_id = PMM_DAEMON_SUBSCRIPTION_ID_INVALID;
    }
    if (!coordinator || !subscription_id || !project_key || project_key[0] == '\0' ||
        client_id == PMM_DAEMON_CLIENT_ID_INVALID) {
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }

    pmm_mutex_lock(&coordinator->mutex);
    if (coordinator->state != PMM_DAEMON_COORDINATOR_RUNNING ||
        !find_client_locked(coordinator, client_id)) {
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }
    pmm_daemon_job_t *job = find_job_locked(coordinator, project_key);
    if (job && job->state != PMM_DAEMON_JOB_RUNNING) {
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }

    bool started = job == NULL;
    pmm_daemon_job_t *new_job = NULL;
    char *key_copy = NULL;
    pmm_daemon_subscription_t *subscription = malloc(sizeof(*subscription));
    if (started) {
        new_job = calloc(1, sizeof(*new_job));
        key_copy = daemon_string_dup(project_key);
    }
    if (!subscription || (started && (!new_job || !key_copy))) {
        free(subscription);
        free(new_job);
        free(key_copy);
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }

    pmm_daemon_subscription_id_t id = issue_subscription_id_locked(coordinator);
    if (id == PMM_DAEMON_SUBSCRIPTION_ID_INVALID) {
        free(subscription);
        free(new_job);
        free(key_copy);
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }
    if (started) {
        new_job->project_key = key_copy;
        new_job->state = PMM_DAEMON_JOB_RUNNING;
        new_job->next = coordinator->jobs;
        coordinator->jobs = new_job;
        coordinator->job_count++;
        job = new_job;
    }
    subscription->id = id;
    subscription->client_id = client_id;
    subscription->next = job->subscriptions;
    job->subscriptions = subscription;
    job->subscription_count++;
    *subscription_id = id;
    pmm_mutex_unlock(&coordinator->mutex);
    return started ? PMM_DAEMON_SUBSCRIPTION_STARTED : PMM_DAEMON_SUBSCRIPTION_JOINED;
}

pmm_daemon_subscription_result_t pmm_daemon_watch_subscribe(
    pmm_daemon_coordinator_t *coordinator, pmm_daemon_client_id_t client_id,
    const char *project_key, pmm_daemon_subscription_id_t *subscription_id) {
    if (subscription_id) {
        *subscription_id = PMM_DAEMON_SUBSCRIPTION_ID_INVALID;
    }
    if (!coordinator || !subscription_id || !project_key || project_key[0] == '\0' ||
        client_id == PMM_DAEMON_CLIENT_ID_INVALID) {
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }

    pmm_mutex_lock(&coordinator->mutex);
    if (coordinator->state != PMM_DAEMON_COORDINATOR_RUNNING ||
        !find_client_locked(coordinator, client_id)) {
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }
    pmm_daemon_watch_t *watch = find_watch_locked(coordinator, project_key);
    bool started = watch == NULL;
    pmm_daemon_watch_t *new_watch = NULL;
    char *key_copy = NULL;
    pmm_daemon_subscription_t *subscription = malloc(sizeof(*subscription));
    if (started) {
        new_watch = calloc(1, sizeof(*new_watch));
        key_copy = daemon_string_dup(project_key);
    }
    if (!subscription || (started && (!new_watch || !key_copy))) {
        free(subscription);
        free(new_watch);
        free(key_copy);
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }

    pmm_daemon_subscription_id_t id = issue_subscription_id_locked(coordinator);
    if (id == PMM_DAEMON_SUBSCRIPTION_ID_INVALID) {
        free(subscription);
        free(new_watch);
        free(key_copy);
        pmm_mutex_unlock(&coordinator->mutex);
        return PMM_DAEMON_SUBSCRIPTION_REJECTED;
    }
    if (started) {
        new_watch->project_key = key_copy;
        new_watch->next = coordinator->watches;
        coordinator->watches = new_watch;
        coordinator->watch_count++;
        watch = new_watch;
    }
    subscription->id = id;
    subscription->client_id = client_id;
    subscription->next = watch->subscriptions;
    watch->subscriptions = subscription;
    watch->subscription_count++;
    *subscription_id = id;
    pmm_mutex_unlock(&coordinator->mutex);
    return started ? PMM_DAEMON_SUBSCRIPTION_STARTED : PMM_DAEMON_SUBSCRIPTION_JOINED;
}

bool pmm_daemon_job_unsubscribe(pmm_daemon_coordinator_t *coordinator,
                                pmm_daemon_client_id_t client_id,
                                pmm_daemon_subscription_id_t subscription_id) {
    if (!coordinator || client_id == PMM_DAEMON_CLIENT_ID_INVALID ||
        subscription_id == PMM_DAEMON_SUBSCRIPTION_ID_INVALID) {
        return false;
    }

    pmm_daemon_callback_batch_t batch;
    pmm_mutex_lock(&coordinator->mutex);
    callback_batch_init_locked(coordinator, &batch);
    if (!find_client_locked(coordinator, client_id)) {
        pmm_mutex_unlock(&coordinator->mutex);
        return false;
    }
    bool removed = false;
    for (pmm_daemon_job_t *job = coordinator->jobs; job; job = job->next) {
        removed = remove_subscription_locked(&job->subscriptions, &job->subscription_count,
                                             client_id, subscription_id);
        if (removed) {
            request_job_cancel_locked(coordinator, job, &batch);
            break;
        }
    }
    pmm_mutex_unlock(&coordinator->mutex);
    callback_batch_run(coordinator, &batch);
    return removed;
}

bool pmm_daemon_watch_unsubscribe(pmm_daemon_coordinator_t *coordinator,
                                  pmm_daemon_client_id_t client_id,
                                  pmm_daemon_subscription_id_t subscription_id) {
    if (!coordinator || client_id == PMM_DAEMON_CLIENT_ID_INVALID ||
        subscription_id == PMM_DAEMON_SUBSCRIPTION_ID_INVALID) {
        return false;
    }

    pmm_daemon_callback_batch_t batch;
    pmm_mutex_lock(&coordinator->mutex);
    callback_batch_init_locked(coordinator, &batch);
    if (!find_client_locked(coordinator, client_id)) {
        pmm_mutex_unlock(&coordinator->mutex);
        return false;
    }
    bool removed = false;
    pmm_daemon_watch_t **cursor = &coordinator->watches;
    while (*cursor) {
        pmm_daemon_watch_t *watch = *cursor;
        removed = remove_subscription_locked(&watch->subscriptions, &watch->subscription_count,
                                             client_id, subscription_id);
        if (!removed) {
            cursor = &watch->next;
            continue;
        }
        if (watch->subscription_count == 0) {
            *cursor = watch->next;
            watch->next = NULL;
            coordinator->watch_count--;
            queue_watch_release_locked(coordinator, watch, &batch);
        }
        break;
    }
    pmm_mutex_unlock(&coordinator->mutex);
    callback_batch_run(coordinator, &batch);
    return removed;
}

size_t pmm_daemon_job_subscribers(pmm_daemon_coordinator_t *coordinator, const char *project_key) {
    if (!coordinator || !project_key) {
        return 0;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_job_t *job = find_job_locked(coordinator, project_key);
    size_t count = job ? job->subscription_count : 0;
    pmm_mutex_unlock(&coordinator->mutex);
    return count;
}

size_t pmm_daemon_watch_subscribers(pmm_daemon_coordinator_t *coordinator,
                                    const char *project_key) {
    if (!coordinator || !project_key) {
        return 0;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_watch_t *watch = find_watch_locked(coordinator, project_key);
    size_t count = watch ? watch->subscription_count : 0;
    pmm_mutex_unlock(&coordinator->mutex);
    return count;
}

size_t pmm_daemon_active_jobs(pmm_daemon_coordinator_t *coordinator) {
    if (!coordinator) {
        return 0;
    }
    pmm_mutex_lock(&coordinator->mutex);
    size_t count = coordinator->job_count;
    pmm_mutex_unlock(&coordinator->mutex);
    return count;
}

size_t pmm_daemon_active_watches(pmm_daemon_coordinator_t *coordinator) {
    if (!coordinator) {
        return 0;
    }
    pmm_mutex_lock(&coordinator->mutex);
    size_t count = coordinator->watch_count;
    pmm_mutex_unlock(&coordinator->mutex);
    return count;
}

pmm_daemon_job_state_t pmm_daemon_job_state(pmm_daemon_coordinator_t *coordinator,
                                            const char *project_key) {
    if (!coordinator || !project_key) {
        return PMM_DAEMON_JOB_NONE;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_job_t *job = find_job_locked(coordinator, project_key);
    pmm_daemon_job_state_t state = job ? job->state : PMM_DAEMON_JOB_NONE;
    pmm_mutex_unlock(&coordinator->mutex);
    return state;
}

bool pmm_daemon_job_reaping(pmm_daemon_coordinator_t *coordinator, const char *project_key) {
    if (!coordinator || !project_key) {
        return false;
    }
    pmm_mutex_lock(&coordinator->mutex);
    pmm_daemon_job_t *job = find_job_locked(coordinator, project_key);
    bool transitioned = job && job->state == PMM_DAEMON_JOB_CANCEL_REQUESTED;
    if (transitioned) {
        job->state = PMM_DAEMON_JOB_REAPING;
    }
    pmm_mutex_unlock(&coordinator->mutex);
    return transitioned;
}

bool pmm_daemon_job_reaped(pmm_daemon_coordinator_t *coordinator, const char *project_key,
                           uint64_t now_ms) {
    (void)now_ms;
    if (!coordinator || !project_key) {
        return false;
    }
    pmm_daemon_job_t *free_after_unlock = NULL;
    pmm_mutex_lock(&coordinator->mutex);
    bool removed = terminal_job_locked(coordinator, project_key, true, &free_after_unlock);
    pmm_mutex_unlock(&coordinator->mutex);
    free_job(free_after_unlock);
    return removed;
}

bool pmm_daemon_job_completed(pmm_daemon_coordinator_t *coordinator, const char *project_key,
                              uint64_t now_ms) {
    (void)now_ms;
    if (!coordinator || !project_key) {
        return false;
    }
    pmm_daemon_job_t *free_after_unlock = NULL;
    pmm_mutex_lock(&coordinator->mutex);
    bool removed = terminal_job_locked(coordinator, project_key, false, &free_after_unlock);
    pmm_mutex_unlock(&coordinator->mutex);
    free_job(free_after_unlock);
    return removed;
}

bool pmm_daemon_should_exit(pmm_daemon_coordinator_t *coordinator, uint64_t now_ms) {
    (void)now_ms;
    if (!coordinator) {
        return false;
    }
    pmm_mutex_lock(&coordinator->mutex);
    bool should_exit = coordinator->state == PMM_DAEMON_COORDINATOR_STOPPING &&
                       coordinator->client_count == 0 && coordinator->job_count == 0 &&
                       coordinator->watch_count == 0 && coordinator->callback_count == 0;
    pmm_mutex_unlock(&coordinator->mutex);
    return should_exit;
}

bool pmm_daemon_frame_header_encode(uint8_t header[PMM_DAEMON_FRAME_HEADER_SIZE],
                                    pmm_daemon_frame_type_t type, uint16_t flags, uint32_t length) {
    if (!header || !frame_type_valid(type) || length > PMM_DAEMON_MAX_FRAME_SIZE) {
        return false;
    }
    header[FRAME_MAGIC_0] = 'C';
    header[FRAME_MAGIC_1] = 'B';
    header[FRAME_MAGIC_2] = 'M';
    header[FRAME_MAGIC_3] = 'D';
    header[FRAME_VERSION] = PMM_DAEMON_RENDEZVOUS_FRAME_VERSION;
    header[FRAME_TYPE] = (uint8_t)type;
    header[FRAME_FLAGS_HI] = (uint8_t)(flags >> 8);
    header[FRAME_FLAGS_LO] = (uint8_t)flags;
    header[FRAME_LENGTH_3] = (uint8_t)(length >> 24);
    header[FRAME_LENGTH_2] = (uint8_t)(length >> 16);
    header[FRAME_LENGTH_1] = (uint8_t)(length >> 8);
    header[FRAME_LENGTH_0] = (uint8_t)length;
    return true;
}

bool pmm_daemon_frame_header_decode(const uint8_t header[PMM_DAEMON_FRAME_HEADER_SIZE],
                                    pmm_daemon_frame_t *frame) {
    if (!header || !frame || header[FRAME_MAGIC_0] != 'C' || header[FRAME_MAGIC_1] != 'B' ||
        header[FRAME_MAGIC_2] != 'M' || header[FRAME_MAGIC_3] != 'D' ||
        header[FRAME_VERSION] != PMM_DAEMON_RENDEZVOUS_FRAME_VERSION) {
        return false;
    }

    pmm_daemon_frame_type_t type = (pmm_daemon_frame_type_t)header[FRAME_TYPE];
    uint32_t length = ((uint32_t)header[FRAME_LENGTH_3] << 24) |
                      ((uint32_t)header[FRAME_LENGTH_2] << 16) |
                      ((uint32_t)header[FRAME_LENGTH_1] << 8) | (uint32_t)header[FRAME_LENGTH_0];
    if (!frame_type_valid(type) || length > PMM_DAEMON_MAX_FRAME_SIZE) {
        return false;
    }

    frame->type = type;
    frame->flags =
        (uint16_t)(((uint16_t)header[FRAME_FLAGS_HI] << 8) | (uint16_t)header[FRAME_FLAGS_LO]);
    frame->length = length;
    return true;
}
