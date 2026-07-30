/*
 * service_patterns.h — Allowlists for HTTP clients, async dispatch, and config accessors.
 *
 * Used during call resolution to classify CALLS edges as:
 *   HTTP_CALLS  — synchronous HTTP client calls
 *   ASYNC_CALLS — async message/task dispatch
 *   CONFIGURES  — config/env access
 *
 * Lookup is O(1) via hash table initialized once at startup.
 */
#ifndef PMM_SERVICE_PATTERNS_H
#define PMM_SERVICE_PATTERNS_H

#include <stdbool.h>

/* Edge type returned by pattern match. */
typedef enum {
    PMM_SVC_NONE = 0,      /* Not a service pattern — use normal CALLS */
    PMM_SVC_HTTP = 1,      /* Synchronous HTTP client call */
    PMM_SVC_ASYNC = 2,     /* Async dispatch (message broker, task queue) */
    PMM_SVC_CONFIG = 3,    /* Config/env accessor */
    PMM_SVC_ROUTE_REG = 4, /* Route registration (router.GET, app.get, Route::post) */
    PMM_SVC_GRPC = 5,      /* gRPC client call (protobuf stub invocation) */
    PMM_SVC_GRAPHQL = 6,   /* GraphQL client query/mutation */
    PMM_SVC_TRPC = 7,      /* tRPC client procedure call */
} pmm_svc_kind_t;

/* Initialize the pattern lookup tables. Call once at startup. Thread-safe after init. */
void pmm_service_patterns_init(void);

/* Check if a resolved QN contains a known service library identifier.
 * Returns the pattern kind, or PMM_SVC_NONE if no match.
 * Matches on library name substrings in the QN (e.g., "requests" in
 * "project.venv.requests.api.get"). Import-alias transparent. */
pmm_svc_kind_t pmm_service_pattern_match(const char *resolved_qn);

/* True for a bare, unqualified call to the native `fetch()` API. Deliberately
 * NOT part of the substring tables above: those are matched unconditionally
 * against the raw callee name before/regardless of registry resolution
 * (the #523 external-library bypass), and "fetch" — unlike "axios" or
 * "requests" — collides with a plausible local identifier. Callers must
 * only consult this after registry resolution has come back empty, so a
 * locally resolvable `function fetch(){}` / `const fetch = () => {}` is
 * classified via its real resolved QN instead and never reaches this check. */
bool pmm_service_pattern_is_global_fetch(const char *callee_name);

/* Per-worker TLS cache for pmm_service_pattern_match results. The
 * pattern matcher runs once per resolved CALL edge in emit_service_
 * edge — that's 6 pattern lists × ~30 patterns × strstr per call ≈
 * ~180 strstrs per call. The same resolved QN repeats across most of
 * the call edges in a project (e.g. "fmt.Errorf"), so caching turns
 * a linear pattern-list scan into one hash lookup. Call _begin once
 * per worker thread before the resolve loop and _end at the end. */
void pmm_service_pattern_cache_begin(void);
void pmm_service_pattern_cache_end(void);

/* Get the HTTP method from the callee name suffix (e.g., ".get" → "GET").
 * Returns NULL if method cannot be inferred. */
const char *pmm_service_pattern_http_method(const char *callee_name);

/* Get the HTTP method from a route registration callee name suffix
 * (e.g., "router.GET" → "GET", "app.post" → "POST").
 * Returns NULL if not a known route registration method. */
const char *pmm_service_pattern_route_method(const char *callee_name);

/* Get the broker name for an async QN (e.g., "pubsub" from a Pub/Sub QN).
 * Returns NULL if not an async pattern. */
const char *pmm_service_pattern_broker(const char *resolved_qn);

#endif /* PMM_SERVICE_PATTERNS_H */
