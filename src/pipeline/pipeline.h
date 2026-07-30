/*
 * pipeline.h — Indexing pipeline orchestrator.
 *
 * Orchestrates multi-pass indexing of a repository:
 *   1. Structure: Project/Folder/Package/File nodes
 *   2. Definitions: Extract + write nodes + build registry
 *   3. Imports: Resolve import edges
 *   4. Calls: Call resolution (registry + LSP)
 *   5. Usages: Usage/type_ref edges
 *   6. Semantic: Inherits/decorates/implements
 *   7. Post: Tests, communities, HTTP links, config, git history
 *
 * Depends on: foundation, extraction, lsp, store, graph_buffer, discover
 */
#ifndef PMM_PIPELINE_H
#define PMM_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "discover/discover.h" /* pmm_ignored_file_t (#963) */

/* Forward declarations */
typedef struct pmm_store pmm_store_t;
typedef struct pmm_gbuf pmm_gbuf_t;

/* ── Opaque handle ──────────────────────────────────────────────── */

typedef struct pmm_pipeline pmm_pipeline_t;

/* ── Index mode ─────────────────────────────────────────────────── */

#ifndef PMM_INDEX_MODE_T_DEFINED
#define PMM_INDEX_MODE_T_DEFINED
typedef enum {
    /* All modes run the LSP type-aware call/usage resolution (per-file +
     * cross-file). The mode only controls file discovery breadth and whether
     * SIMILAR_TO / SEMANTICALLY_RELATED edges are computed. */
    PMM_MODE_FULL = 0,     /* Full: everything including SIMILAR_TO + SEMANTICALLY_RELATED */
    PMM_MODE_MODERATE = 1, /* Moderate: fast discovery + SIMILAR_TO + SEMANTICALLY_RELATED */
    PMM_MODE_FAST = 2,     /* Fast: skip non-essential files, no similarity/semantic edges */
} pmm_index_mode_t;
#endif

/* ── Pipeline lifecycle ─────────────────────────────────────────── */

/* Create a new pipeline. Caller owns the result. */
pmm_pipeline_t *pmm_pipeline_new(const char *repo_path, const char *db_path, pmm_index_mode_t mode);

/* Enable persistent artifact export (.project-mind-mcp/graph.db.zst).
 * When enabled, the pipeline writes a compressed artifact after indexing. */
void pmm_pipeline_set_persistence(pmm_pipeline_t *p, bool enabled);

/* Free a pipeline and all its internal state. NULL-safe. */
void pmm_pipeline_free(pmm_pipeline_t *p);

/* Run the full indexing pipeline. Returns 0 on success, -1 on error.
 * Discovers files, extracts, resolves, and dumps to SQLite. */
int pmm_pipeline_run(pmm_pipeline_t *p);

/* Request cancellation of a running pipeline (thread-safe). */
void pmm_pipeline_cancel(pmm_pipeline_t *p);

/* Bind cancellation to a caller-owned atomic flag. The flag must outlive the
 * pipeline and should be initialized before binding. This lets a long-lived
 * daemon request cancellation without retaining/dereferencing a pipeline
 * pointer that its request thread may concurrently retire. */
void pmm_pipeline_bind_cancel_flag(pmm_pipeline_t *p, atomic_int *cancelled);

/* Get the project name derived from repo_path. Returned string is
 * owned by the pipeline. Valid until pmm_pipeline_free(). */
const char *pmm_pipeline_project_name(const pmm_pipeline_t *p);

/* Override the derived project name with a sanitized user-provided label. */
bool pmm_pipeline_set_project_name(pmm_pipeline_t *p, const char *name);

/* Get the index mode (PMM_MODE_FULL, PMM_MODE_MODERATE, PMM_MODE_FAST). */
int pmm_pipeline_get_mode(const pmm_pipeline_t *p);

/* Get the list of directory subtrees skipped during discovery (#411).
 * *out receives a borrowed array of rel-path strings (owned by the pipeline,
 * valid until pmm_pipeline_free()); *count receives its length. Both are set
 * to NULL/0 when p is NULL or nothing was excluded. Do not free. */
void pmm_pipeline_get_excluded(const pmm_pipeline_t *p, char ***out, int *count);

/* Committed node/edge counts captured at dump time (-1 when dump did not run).
 * Nodes are the #334 plausibility-gate axis; edges are informational only. */
void pmm_pipeline_get_committed_counts(const pmm_pipeline_t *p, int *nodes, int *edges);

/* ── Per-file indexing failures (Stage 2 / Track B) ─────────────── */

/* One source file that was skipped during indexing. All strings are owned by
 * the pipeline (copied on record, freed in pmm_pipeline_free). A skip is the
 * expected, handled outcome of a bad/oversized file — indexing continues and
 * the run still reports status "indexed"; these are surfaced (not errors that
 * fail the run) via MCP `skipped[]` / the CLI / a per-run logfile. */
typedef struct {
    char *path;   /* repo-relative path of the skipped file */
    char *reason; /* human-readable cause (e.g. "oversized (712 MB > 512 MB)",
                   * "parse timeout", "read failed"). For phase "parse_partial"
                   * this carries the 1-based line-range list ("12-40,88-90")
                   * of the unparseable regions. */
    char *phase;  /* "read" | "extract" | "oversized" | "parse_partial".
                   * "parse_partial" (#963) is NOT a skip: the file WAS indexed
                   * but contains tree-sitter ERROR/MISSING regions whose
                   * constructs are absent from the graph (best-effort signal —
                   * absence of the flag is NOT a completeness guarantee). The
                   * MCP layer reports it separately from skipped[]. "cross_lsp"
                   * is a RESERVED phase string for Track C's crash-attribution
                   * signal and is intentionally NOT emitted today (the
                   * cross-LSP passes are best-effort/void with no genuine
                   * per-file failure). */
} pmm_file_error_t;

/* Record a skipped file. path/reason/phase are copied. NULL-safe on p.
 *
 * NOT thread-safe: call it from the sequential extraction pass, or from the
 * parallel merge step (never from inside a parallel worker — workers collect
 * into per-worker lists and merge sequentially). */
void pmm_pipeline_add_file_error(pmm_pipeline_t *p, const char *path, const char *reason,
                                 const char *phase);

/* Borrowed accessor for the recorded skips (owned by the pipeline, valid until
 * pmm_pipeline_free()). out and count are set to NULL and 0 when p is NULL or
 * nothing was skipped. Do not free. */
void pmm_pipeline_get_file_errors(const pmm_pipeline_t *p, pmm_file_error_t **out, int *count);

/* Borrowed accessor for the individually-ignored files captured during
 * discovery (#963 "purposely not indexed" — by design, not failures). count
 * is the stored (capped) length, total the uncapped number seen. Do not
 * free. */
void pmm_pipeline_get_ignored(const pmm_pipeline_t *p, pmm_ignored_file_t **out, int *count,
                              int *total);

/* ── Index lock (prevents concurrent pipeline runs on same DB) ──── */

/* Try to acquire the global index lock. Returns true if acquired,
 * false if another pipeline is already running (non-blocking).
 * Use this in the watcher — skip reindex if busy. */
bool pmm_pipeline_try_lock(void);

/* Acquire the global index lock, blocking until available.
 * Use this in MCP handler and autoindex — wait for busy watcher to finish. */
void pmm_pipeline_lock(void);

/* Release the global index lock. */
void pmm_pipeline_unlock(void);

/* ── FQN helpers (used by passes and external callers) ──────────── */

/* Compute a qualified name: project.dir.parts.name
 * Strips extension, converts / to ., drops __init__ and index.
 * Caller must free() the returned string. */
char *pmm_pipeline_fqn_compute(const char *project, const char *rel_path, const char *name);

/* Module QN: project.dir.parts (no name). Caller must free(). */
char *pmm_pipeline_fqn_module(const char *project, const char *rel_path);

/* Language-aware module QN. When `module_is_dir` is true (Java/Go package
 * semantics) the module is derived from the CONTAINING DIRECTORY (the filename
 * stem is dropped), so it agrees with the extraction-side def QNs; when false
 * it is exactly pmm_pipeline_fqn_module(). Caller must free(). */
char *pmm_pipeline_fqn_module_dir(const char *project, const char *rel_path, bool module_is_dir);

/* Folder QN: project.dir.parts. Caller must free(). */
char *pmm_pipeline_fqn_folder(const char *project, const char *rel_dir);

/* Resolve an import specifier that uses a relative path (./foo, ../bar, .foo,
 * or an unqualified local name like "foo.h") against the importing file's
 * path.  Returns a malloc'd normalized relative path without extension
 * (e.g. "src/api/helpers") suitable for passing to pmm_pipeline_fqn_module,
 * or NULL if the specifier is not a relative path (bare module names like
 * "lodash", "django", "github.com/foo/bar" return NULL — the caller should
 * treat those as external/unresolvable). Handles ".", "..", and leading
 * dot-only segments used by Python relative imports. */
char *pmm_pipeline_resolve_relative_import(const char *source_rel, const char *module_path);

/* Derive project name from an absolute path.
 * Replaces / and : with -, collapses --, trims leading -.
 * Caller must free() the returned string. */
char *pmm_project_name_from_path(const char *abs_path);

/* ── Function Registry ──────────────────────────────────────────── */

typedef struct pmm_registry pmm_registry_t;

typedef struct {
    const char *qualified_name; /* borrowed from registry */
    const char *strategy;       /* resolution strategy name */
    double confidence;          /* 0.0–1.0 */
    int candidate_count;
} pmm_resolution_t;

/* Create/free a function registry. */
pmm_registry_t *pmm_registry_new(void);
void pmm_registry_free(pmm_registry_t *r);

/* Register a function/method/class. All strings are copied. */
void pmm_registry_add(pmm_registry_t *r, const char *name, const char *qualified_name,
                      const char *label);

/* Resolve a callee name using prioritized strategies.
 * import_map: NULL-terminated array of {local_name, resolved_qn} pairs, or NULL.
 * Returns result with qualified_name="" if unresolved. */
pmm_resolution_t pmm_registry_resolve(const pmm_registry_t *r, const char *callee_name,
                                      const char *module_qn, const char **import_map_keys,
                                      const char **import_map_vals, int import_map_count);

/* Per-file memoization cache for is_import_reachable. Thread-local —
 * each resolve worker owns its own cache. Call _begin at the start
 * of resolve_file_calls (or any per-file resolve loop) and _end at
 * the end. The cache MUST be invalidated between files because
 * is_import_reachable's truth depends on the file's import_vals. */
void pmm_registry_reach_cache_begin(int estimated_capacity);
void pmm_registry_reach_cache_end(void);

/* Per-file import-map prefix → module-QN hash. Turns the linear
 * strcmp scan inside resolve_import_map into O(1). Keys/values are
 * BORROWED — caller must keep the import_map arrays alive for the
 * cache lifetime. Invalidate between files via _end. */
void pmm_registry_import_map_cache_begin(const char **keys, const char **vals, int count);
void pmm_registry_import_map_cache_end(void);

/* Per-file full-result cache for pmm_registry_resolve. The same
 * callee_name appears in many call sites within a file; module_qn
 * is constant per file so each name resolves identically. First
 * lookup does the full strategy chain; repeats are O(1) hash hits.
 * This eliminates ~75% of the resolve-chain work on K8s where the
 * same names ("Get", "Add", "New", etc) appear hundreds of times. */
void pmm_registry_resolve_cache_begin(int estimated_capacity);
void pmm_registry_resolve_cache_end(void);

/* Check if a qualified name exists in the registry. */
bool pmm_registry_exists(const pmm_registry_t *r, const char *qn);

/* True if `name` is one of the curated Perl core builtins (perlfunc). Used by
 * the call-resolution passes to suppress generic-resolver CALLS edges from Perl
 * builtin invocations (push/shift/keys/...) to project subs that merely share
 * the name. Perl-scoped: callers gate on the file language. */
bool pmm_perl_is_builtin(const char *name);

/* Decide whether a resolved Perl call edge is generic-resolver noise to drop
 * (#476): true only for Perl, only for a builtin/method call, and only when the
 * match used a weak short-name strategy — high-confidence same_module/import_map
 * matches are kept. Pure; unit-tested in test_registry.c. */
bool pmm_perl_suppress_generic_match(bool is_perl, bool is_method, const char *callee_name,
                                     const char *strategy);

/* Decide whether a resolved TS/JS/TSX member-call edge is weak-strategy noise to
 * drop (#592/#606): true only for TS/JS, only for a member call with a
 * non-this/super receiver (is_method), and only when the match used a weak
 * short-name strategy (suffix_match / unique_name / field_type_hint / fuzzy).
 * Explicit drop-list keeps every lsp_* / import / same-module / qualified match.
 * Pure; unit-tested in test_registry.c. */
bool pmm_tsjs_suppress_weak_method_match(bool is_tsjs, bool is_method, const char *strategy);

/* Get the label of a qualified name, or NULL if not found. */
const char *pmm_registry_label_of(const pmm_registry_t *r, const char *qn);

/* Find all QNs with a given simple name. Sets *out and *count.
 * Caller does NOT free the array (owned by registry). */
int pmm_registry_find_by_name(const pmm_registry_t *r, const char *name, const char ***out,
                              int *count);

/* Return total number of entries. */
int pmm_registry_size(const pmm_registry_t *r);

/* Find all qualified names ending with ".suffix".
 * Sets *out to heap-allocated array of borrowed string pointers.
 * Caller must free(*out) but NOT the individual strings.
 * Returns count of matches. */
int pmm_registry_find_ending_with(const pmm_registry_t *r, const char *suffix, const char ***out);

/* Check if candidate QN's module prefix is reachable via any import value. */
bool pmm_registry_is_import_reachable(const char *candidate_qn, const char **import_vals,
                                      int import_count);

/* Fuzzy resolve: match callee by bare function name (last segment after dots).
 * Returns result with ok=true if found, ok=false if not.
 * Lower confidence than Resolve (0.40 single, 0.30 multiple). */
typedef struct {
    pmm_resolution_t result;
    bool ok;
} pmm_fuzzy_result_t;

pmm_fuzzy_result_t pmm_registry_fuzzy_resolve(const pmm_registry_t *r, const char *callee_name,
                                              const char *module_qn, const char **import_map_keys,
                                              const char **import_map_vals, int import_map_count);

const char *pmm_confidence_band(double score);

#endif /* PMM_PIPELINE_H */
