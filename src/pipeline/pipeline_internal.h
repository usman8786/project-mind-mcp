#include "foundation/constants.h"
/*
 * pipeline_internal.h — Internal pipeline state shared between pass files.
 *
 * NOT a public header. Only included by pipeline.c and pass_*.c files.
 * Exposes the pipeline context struct for direct field access by passes.
 */
#ifndef PMM_PIPELINE_INTERNAL_H
#define PMM_PIPELINE_INTERNAL_H

#include "pipeline/pipeline.h"
#include "pipeline/path_alias.h"
#include "graph_buffer/graph_buffer.h"
#include "discover/discover.h"
#include "foundation/hash_table.h"
#include "pmm.h"
#include "lsp/go_lsp.h" /* CBMLSPDef for pmm_parallel_resolve cross-LSP inputs */
#include <stdatomic.h>
#include <string.h>

/* ── Shared pipeline constants ─────────────────────────────────── */

/* Maximum byte budget for tree-sitter extraction per file */
#define PMM_EXTRACT_BUDGET 5000000

/* Route node QN buffer size (must fit __route__METHOD__/full/url/path) */
#define PMM_ROUTE_QN_SIZE 768

/* Incremental integrity failure: abort the run and preserve the existing DB.
 * Distinct from PMM_NOT_FOUND, which the orchestrator uses as the normal
 * "no incremental route; continue with a full index" sentinel. */
#define PMM_PIPELINE_ABORT_PRESERVE_DB (-2)

/* Canonicalize route-path parameter placeholders (":id", "{id}", "<id>",
 * "${...}") to a single "{}" token so that client call sites and server
 * handlers rendezvous on the same Route QN regardless of framework syntax.
 * Parameter names are intentionally discarded ("/u/{id}" and "/u/{slug}" both
 * canonicalize to "/u/{}"). The result never exceeds the input length, so
 * out_sz >= strlen(in) + 1 always suffices. Returns out. */
const char *pmm_route_canon_path(const char *in, char *out, size_t out_sz);

/* True when a graph node is a structural directory container (Folder/Project)
 * rather than a code node. In a directory-based-module language (Java/Go, see
 * pmm_lang_module_is_dir) a file's module QN equals its directory QN, so an
 * enclosing-scope lookup for a CLASS-LEVEL usage/call (enclosing_func_qn ==
 * module_qn) resolves to the ONE Folder/Project node shared by every file in
 * that package. Sourcing an edge there conflates all same-package files into a
 * single source node with an arbitrary file_path (#787). Source-node finders
 * must treat such a hit as a miss and fall back to the per-file File node. */
static inline bool pmm_pipeline_node_is_dir_container(const pmm_gbuf_node_t *node) {
    return node && node->label &&
           (strcmp(node->label, "Folder") == 0 || strcmp(node->label, "Project") == 0);
}

/* Time unit conversions */
#define PMM_NS_PER_SEC 1000000000LL
#define PMM_US_PER_SEC 1000000LL
#define PMM_MS_PER_SEC 1000.0
#define PMM_US_PER_SEC_F 1e6

/* ── Pipeline context (internal) ─────────────────────────────────── */

/* Per-worker manifest collection entry. */
typedef struct {
    char *pkg_name;  /* heap: "@myorg/pkg", "github.com/foo/bar" */
    char *entry_rel; /* heap: "packages/pkg/src/index" (no extension) */
} pmm_pkg_entry_t;

/* Growable array of package entries (per-worker, no thread contention). */
typedef struct {
    pmm_pkg_entry_t *items;
    int count;
    int cap;
} pmm_pkg_entries_t;

void pmm_pkg_entries_init(pmm_pkg_entries_t *e);
void pmm_pkg_entries_free(pmm_pkg_entries_t *e);

/* Shared context passed to each pass function.
 * Derived from pmm_pipeline_t fields during run. */
typedef struct {
    const char *project_name; /* borrowed from pipeline */
    const char *repo_path;    /* borrowed from pipeline */
    pmm_gbuf_t *gbuf;         /* owned by pipeline */
    pmm_registry_t *registry; /* owned by pipeline */
    atomic_int *cancelled;    /* pointer to pipeline's cancelled flag */
    pmm_pipeline_t *pipeline; /* back-pointer for recording per-file skips
                               * (Stage 2 / Track B). May be NULL on paths that
                               * don't record; pmm_pipeline_add_file_error is
                               * NULL-safe. */
    int mode;                 /* pmm_index_mode_t (0=full, 1=moderate, 2=fast, 3=advanced) */

    /* Extraction result cache (sequential pipeline optimization).
     * When non-NULL, pass_definitions stores results here instead of freeing,
     * and pass_calls/usages/semantic reuse cached results instead of re-extracting.
     * Indexed by file position in the files[] array. Owned by pipeline.c. */
    CBMFileResult **result_cache;

    /* Build-tool path aliases (tsconfig/jsconfig today; webpack/vite-style
     * configs are an easy follow-on). NULL when no usable configs were found.
     * Owned by pipeline.c / pipeline_incremental.c. */
    const pmm_path_alias_collection_t *path_aliases;

    /* Directory subtrees excluded during discovery. Borrowed from pipeline.c. */
    char **excluded_dirs;
    int excluded_count;

    /* Sequential cross-LSP registry arena. The lsp_cross pass builds its
     * shared per-language registries here; resolved_calls entries may BORROW
     * strings owned by these registries, and the later calls pass still
     * reads them — so the arena is OWNED and destroyed by
     * run_sequential_pipeline AFTER all passes, never by the lsp_cross pass
     * itself (destroying at pass end was a use-after-free in pass_calls).
     * Mirrors the parallel path, where cross_lsp_arena outlives the fused
     * resolve. */
    CBMArena seq_cross_arena;
    bool seq_cross_arena_live;

    /* ObjectScript $$$macro table built from .inc files in the repo (NULL if
     * no ObjectScript include files were found). Owned by pipeline.c. */
    const CBMMacroTable *macro_table;

    /* ObjectScript method-return-type table built from extracted definitions
     * (NULL until pass_calls builds it). Owned by pipeline.c. */
    const CBMReturnTypeTable *return_type_table;
} pmm_pipeline_ctx_t;

static inline int pmm_pipeline_relpath_is_excluded(const char *rel_path, char *const *excluded_dirs,
                                                   int excluded_count) {
    if (!rel_path || rel_path[0] == '\0' || !excluded_dirs || excluded_count <= 0) {
        return 0;
    }
    for (int i = 0; i < excluded_count; i++) {
        const char *excluded = excluded_dirs[i];
        if (!excluded || excluded[0] == '\0') {
            continue;
        }
        size_t n = strlen(excluded);
        if (strncmp(rel_path, excluded, n) == 0 && (rel_path[n] == '\0' || rel_path[n] == '/')) {
            return SKIP_ONE;
        }
    }
    return 0;
}

/* Get the current pipeline's package map (NULL if none). */
CBMHashTable *pmm_pipeline_get_pkgmap(void);
void pmm_pipeline_set_pkgmap(CBMHashTable *map);

/* Unified module resolver: relative → pkgmap → fqn_module fallback.
 * Handles bare specifiers via pkgmap lookup with prefix matching.
 * Caller must free() the returned string. */
char *pmm_pipeline_resolve_module(const pmm_pipeline_ctx_t *ctx, const char *source_rel,
                                  const char *module_path);

/* Resolve an import to its in-graph target node, or NULL if unresolvable.
 *
 * Resolution order (first hit wins):
 *   1. Module-path resolution (relative / pkgmap / fqn_module) → existing node.
 *      This preserves the behavior for Python/TS/Go whose module path maps
 *      directly to a sibling Module/File QN.
 *   2. namespace_map[module_path-prefix] → File node QN (Java/Kotlin/C#/PHP
 *      `using`/`import` of a NAMESPACE that the path-based QN cannot express).
 *   3. Symbol-name fallback: the import's last path segment matched against an
 *      in-graph definition node of the same simple name in a different file
 *      (Rust `use crate::util::helper`, Java `import com.example.Util`, ...).
 *
 * `namespace_map` may be NULL (skips step 2).  `source_file_qn` is the importing
 * file's __file__ QN, used to avoid self-imports in step 3. */
const pmm_gbuf_node_t *pmm_pipeline_resolve_import_node(const pmm_pipeline_ctx_t *ctx,
                                                        const char *source_rel,
                                                        const char *source_file_qn,
                                                        const CBMImport *imp,
                                                        CBMHashTable *namespace_map);

/* Build a namespace → File-node-QN map from a set of extraction results.
 * Each result that declared a namespace/package contributes one entry keyed by
 * the namespace string (e.g. "App.Utils", "com.example").  Returns NULL when no
 * results declared a namespace.  Caller frees via pmm_pipeline_namespace_map_free. */
CBMHashTable *pmm_pipeline_namespace_map_build(const char *project_name,
                                               CBMFileResult *const *results,
                                               const char *const *rels, int count);
void pmm_pipeline_namespace_map_free(CBMHashTable *map);

/* Parse a manifest file and collect pkg entries. Returns true if basename matched. */
bool pmm_pkgmap_try_parse(const char *basename, const char *rel_path, const char *source,
                          int source_len, pmm_pkg_entries_t *entries);

/* Merge per-worker entries into a hash table. Returns NULL if no entries. */
CBMHashTable *pmm_pkgmap_build(pmm_pkg_entries_t *worker_entries, int worker_count,
                               const char *project_name);

/* Build pkgmap by reading manifest files from the files array (sequential path). */
int pmm_pkgmap_scan_repo(const char *repo_path, pmm_pkg_entries_t *entries, char **excluded_dirs,
                         int excluded_count);
CBMHashTable *pmm_pkgmap_build_from_repo(const char *repo_path, const pmm_file_info_t *files,
                                         int file_count, const char *project_name,
                                         char **excluded_dirs, int excluded_count);
CBMHashTable *pmm_pkgmap_build_from_files(const pmm_file_info_t *files, int file_count,
                                          const char *project_name);

/* Free pkgmap and all owned strings. */
void pmm_pkgmap_free(CBMHashTable *pkgmap);

/* Check cancellation. Returns non-zero if cancelled. */
static inline int pmm_pipeline_check_cancel(const pmm_pipeline_ctx_t *ctx) {
    return atomic_load(ctx->cancelled) ? PMM_NOT_FOUND : 0;
}

/* ── Testable helpers ────────────────────────────────────────────── */

/* Check if a file path is worth tracking for git history analysis. */
bool pmm_is_trackable_file(const char *path);

/* Check if a file path looks like a test file (language-agnostic). */
bool pmm_is_test_path(const char *path);

/* Check if a function name looks like a test function (language-agnostic). */
bool pmm_is_test_func_name(const char *name);

/* Coupling result from computeChangeCoupling */
typedef struct {
    char file_a[PMM_SZ_512];
    char file_b[PMM_SZ_512];
    int co_change_count;
    double coupling_score;
    /* Unix epoch of the most recent commit that touched both files together.
     * 0 when no timestamp was available (e.g. older callers / popen path
     * without %ct). */
    long long last_co_change;
} pmm_change_coupling_t;

/* Commit data for coupling analysis */
typedef struct {
    char **files;
    int count;
    /* Unix epoch of the commit. 0 means unknown — coupling computation
     * still works but last_co_change on the resulting edge will be 0. */
    long long timestamp;
} pmm_commit_files_t;

/* Per-file temporal metadata. Populated alongside change-coupling so File
 * nodes can carry change_count and last_modified for hotspot / risk
 * analysis queries. */
typedef struct {
    char file_path[PMM_SZ_512];
    int change_count;
    long long last_modified; /* unix epoch of most recent commit */
} pmm_file_temporal_t;

/* Compute change coupling from commit history.
 * Returns number of couplings written to out (up to max_out).
 * Caller owns out[]. */
int pmm_compute_change_coupling(const pmm_commit_files_t *commits, int commit_count,
                                pmm_change_coupling_t *out, int max_out);

/* Go-style implicit interface satisfaction on graph buffer.
 * Finds Interface nodes, matches method sets against Class nodes,
 * creates IMPLEMENTS + OVERRIDE edges. Returns edge count created. */
int pmm_pipeline_implements_go(pmm_pipeline_ctx_t *ctx);

/* Edge type for an explicit base-class relation, keyed off the resolved
 * TARGET node's label: Interface → IMPLEMENTS, anything else → INHERITS.
 * The single decision point for BOTH the sequential semantic pass and the
 * parallel per-file resolve — the two venues must never diverge. */
const char *pmm_semantic_base_edge_type(const pmm_gbuf_node_t *base_node);

/* Explicit-language override detection on the full graph (serial tail).
 * For every IMPLEMENTS/INHERITS edge whose source is a non-Go class, matches
 * the class's DEFINES_METHOD children by name against the base's and creates
 * Method→Method OVERRIDE edges (Java @Override, TS/C#/Kotlin override, PHP
 * redefinition). Go is excluded: implicit satisfaction already covers it.
 * Returns edge count created. */
int pmm_pipeline_override_explicit(pmm_pipeline_ctx_t *ctx);

/* ── Git diff helpers (pass_gitdiff.c) ───────────────────────────── */

typedef struct {
    char status[PMM_SZ_4]; /* M/A/D/R */ /* "M", "A", "D", "R" */
    char path[PMM_SZ_512];
    char old_path[PMM_SZ_512]; /* non-empty only for renames */
} pmm_changed_file_t;

typedef struct {
    char path[PMM_SZ_512];
    int start_line;
    int end_line;
} pmm_changed_hunk_t;

/* Parse git diff --name-status output. Returns count written to out. */
int pmm_parse_name_status(const char *output, pmm_changed_file_t *out, int max_out);

/* Parse git diff --unified=0 output. Returns count written to out. */
int pmm_parse_hunks(const char *output, pmm_changed_hunk_t *out, int max_out);

/* Parse "start,count" or "start" → (start, count). */
void pmm_parse_range(const char *s, int *out_start, int *out_count);

/* ── Config helpers (pass_configures.c) ──────────────────────────── */

/* Check if a string looks like an environment variable name
 * (uppercase + underscore + digits, at least 2 chars with uppercase). */
bool pmm_is_env_var_name(const char *s);

/* Normalize a config key: split camelCase/snake/dots, lowercase.
 * Writes normalized form to norm_out (underscore-joined).
 * Returns token count. tokens_out[] receives borrowed pointers into norm_out. */
int pmm_normalize_config_key(const char *key, char *norm_out, size_t norm_sz);

/* Check if a file path has a config file extension (.toml, .yaml, .env, etc.) */
bool pmm_has_config_extension(const char *path);

/* ── Enrichment helpers (pass_enrichment.c) ──────────────────────── */

/* Split camelCase string on lowercase→uppercase transitions.
 * Writes substrings to out[]. Returns count. Caller must free each out[i]. */
int pmm_split_camel_case(const char *s, char **out, int max_out);

/* Tokenize a decorator into lowercase words, filtering stopwords.
 * E.g. "@login_required" → ["login", "required"].
 * Writes words to out[]. Returns count. Caller must free each out[i]. */
int pmm_tokenize_decorator(const char *dec, char **out, int max_out);

/* ── Compile commands helpers (pass_compile_commands.c) ──────────── */

typedef struct {
    char **include_paths;
    int include_count;
    char **defines;
    int define_count;
    char standard[PMM_SZ_32];
} pmm_compile_flags_t;

/* Split a shell command string into arguments (handles quoting).
 * Writes args to out[]. Returns count. Caller must free each out[i]. */
int pmm_split_command(const char *cmd, char **out, int max_out);

/* Extract -I, -isystem, -D, -std= flags from compiler arguments.
 * Caller must free result with pmm_compile_flags_free(). */
pmm_compile_flags_t *pmm_extract_flags(const char **args, int argc, const char *directory);

/* Free a compile_flags_t allocated by pmm_extract_flags(). */
void pmm_compile_flags_free(pmm_compile_flags_t *f);

/* Parse compile_commands.json content. Returns map as parallel arrays.
 * out_paths[i] is the relative file path, out_flags[i] is its flags.
 * Returns count. Caller must free out_paths[i] and pmm_compile_flags_free(out_flags[i]). */
int pmm_parse_compile_commands(const char *json_data, const char *repo_path, char ***out_paths,
                               pmm_compile_flags_t ***out_flags);

/* ── Infrascan helpers (pass_infrascan.c) ─────────────────────────── */

/* File identification helpers */
bool pmm_is_dockerfile(const char *name);
bool pmm_is_compose_file(const char *name);
bool pmm_is_cloudbuild_file(const char *name);
bool pmm_is_env_file(const char *name);
bool pmm_is_shell_script(const char *name, const char *ext);
bool pmm_is_kustomize_file(const char *name);
bool pmm_is_k8s_manifest(const char *name, const char *content);

/* Secret detection */
bool pmm_is_secret_binding(const char *key, const char *value);
bool pmm_is_secret_value(const char *value);

/* Clean JSON array brackets from CMD/ENTRYPOINT values.
 * E.g. ["./app", "--flag"] → ./app --flag
 * Writes result to out (up to out_sz). */
void pmm_clean_json_brackets(const char *s, char *out, size_t out_sz);

/* Key-value pair for environment variables / config entries */
typedef struct {
    char key[PMM_SZ_128];
    char value[PMM_SZ_512];
} pmm_env_kv_t;

/* Dockerfile parsing result */
typedef struct {
    char base_image[PMM_SZ_256];
    char stage_images[PMM_SZ_16][PMM_SZ_256];
    char stage_names[PMM_SZ_16][PMM_SZ_128];
    int stage_count;
    char exposed_ports[PMM_SZ_16][PMM_SZ_32];
    int port_count;
    pmm_env_kv_t env_vars[PMM_SZ_64];
    int env_count;
    char build_args[PMM_SZ_32][PMM_SZ_128];
    int build_arg_count;
    char workdir[PMM_SZ_256];
    char cmd[PMM_SZ_512];
    char entrypoint[PMM_SZ_512];
    char healthcheck[PMM_SZ_512];
    char user[PMM_SZ_64];
} pmm_dockerfile_result_t;

/* Dotenv parsing result */
typedef struct {
    pmm_env_kv_t env_vars[PMM_SZ_64];
    int env_count;
} pmm_dotenv_result_t;

/* Shell script parsing result */
typedef struct {
    char shebang[PMM_SZ_256];
    pmm_env_kv_t env_vars[PMM_SZ_64];
    int env_count;
    char sources[PMM_SZ_16][PMM_SZ_256];
    int source_count;
    char docker_cmds[PMM_SZ_16][PMM_SZ_256];
    int docker_cmd_count;
} pmm_shell_result_t;

/* Terraform variable */
typedef struct {
    char name[PMM_SZ_128];
    char type[PMM_SZ_64];
    char default_val[PMM_SZ_256];
    char description[PMM_SZ_256];
} pmm_tf_variable_t;

/* Terraform resource / data source */
typedef struct {
    char type[PMM_SZ_128];
    char name[PMM_SZ_128];
} pmm_tf_resource_t;

/* Terraform module */
typedef struct {
    char tf_name[PMM_SZ_128];
    char source[PMM_SZ_256];
} pmm_tf_module_t;

/* Terraform parsing result */
typedef struct {
    pmm_tf_resource_t resources[PMM_SZ_32];
    int resource_count;
    pmm_tf_variable_t variables[PMM_SZ_32];
    int variable_count;
    char outputs[PMM_SZ_32][PMM_SZ_128];
    int output_count;
    char providers[PMM_SZ_16][PMM_SZ_128];
    int provider_count;
    pmm_tf_module_t modules[PMM_SZ_16];
    int module_count;
    pmm_tf_resource_t data_sources[PMM_SZ_16];
    int data_source_count;
    char backend[PMM_SZ_128];
    bool has_locals;
} pmm_terraform_result_t;

/* Parse a Dockerfile from source text. Returns 0 if parsed, -1 if empty/invalid. */
int pmm_parse_dockerfile_source(const char *source, pmm_dockerfile_result_t *out);

/* Parse a .env file from source text. Returns 0 if parsed, -1 if empty. */
int pmm_parse_dotenv_source(const char *source, pmm_dotenv_result_t *out);

/* Parse a shell script from source text. Returns 0 if parsed, -1 if empty. */
int pmm_parse_shell_source(const char *source, pmm_shell_result_t *out);

/* Parse a Terraform file from source text. Returns 0 if parsed, -1 if empty. */
int pmm_parse_terraform_source(const char *source, pmm_terraform_result_t *out);

/* Helm Chart.yaml parse result: chart name + dependency chart names (#338). */
enum { PMM_HELM_MAX_DEPS = 128, PMM_HELM_NAME_MAX = 128 };
typedef struct {
    char chart_name[PMM_HELM_NAME_MAX];
    char deps[PMM_HELM_MAX_DEPS][PMM_HELM_NAME_MAX];
    int dep_count;
} pmm_helm_chart_t;

/* Parse a Helm Chart.yaml: top-level `name:` and `dependencies:` list names.
 * Returns 0 if parsed (name or deps found), -1 otherwise. */
int pmm_parse_helm_chart(const char *source, pmm_helm_chart_t *out);

/* Build an infrastructure QN. Caller must free the returned string. */
char *pmm_infra_qn(const char *project_name, const char *rel_path, const char *infra_type,
                   const char *service_name);

/* ── Parallel pipeline prototypes (pass_parallel.c) ─────────────── */

/* Phase 3A: Parallel extract + create definition nodes.
 * Each worker creates nodes in a per-worker gbuf, then merges into ctx->gbuf.
 * Caches CBMFileResult* in result_cache[file_idx] for reuse in Phase 3B/4.
 * shared_ids provides globally unique node/edge IDs across workers. */

/* Source-retention tuning for pmm_parallel_extract_ex. Zero-valued byte caps
 * mean "use the derived default" (RAM-fraction total, clamped to an absolute
 * ceiling; modest per-file cap); PMM_RETAIN_TOTAL_MB / PMM_RETAIN_PER_FILE_MB
 * override those. retain_sources_set=false keeps the default retain policy. */
typedef struct {
    bool retain_sources;
    bool retain_sources_set; /* false keeps the default retain_sources policy */
    size_t retain_total_budget_bytes;
    size_t retain_per_file_max_bytes;
} pmm_parallel_extract_opts_t;

int pmm_parallel_extract_ex(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count,
                            CBMFileResult **result_cache, _Atomic int64_t *shared_ids,
                            int worker_count, const pmm_parallel_extract_opts_t *opts);
int pmm_parallel_extract(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count,
                         CBMFileResult **result_cache, _Atomic int64_t *shared_ids,
                         int worker_count);

/* Phase 3B: Serial registry build from cached extraction results.
 * Creates DEFINES, DEFINES_METHOD, and IMPORTS edges in ctx->gbuf.
 * Registers callable symbols (Function/Method/Class) in ctx->registry. */
int pmm_build_registry_from_cache(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files,
                                  int file_count, CBMFileResult **result_cache);

/* Phase 4: Parallel call/usage/semantic resolution.
 * Each worker resolves calls, usages, throws, rw, inherits, decorates,
 * and implements edges into per-worker edge bufs, then merges.
 * Runs Go-style implicit IMPLEMENTS as serial post-step. */
/* Opaque module-def index — defined in pass_lsp_cross.c. Forward-declared
 * here so we can include it in pmm_parallel_resolve's signature without
 * pulling the pass header into every consumer of pipeline_internal.h. */
struct CBMModuleDefIndex;

/* pmm_parallel_resolve's cross_registries param is typed `void*` to avoid
 * pulling lsp/go_lsp.h into every TU that includes pipeline_internal.h.
 * Callers cast a CBMCrossLspRegistries* (defined in pass_lsp_cross.h). */

int pmm_parallel_resolve(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count,
                         CBMFileResult **result_cache, _Atomic int64_t *shared_ids,
                         int worker_count,
                         /* Cross-file LSP inputs — pre-built once by the caller and
                          * shared read-only across workers (typed non-const to match
                          * the existing pmm_run_X_lsp_cross signatures the resolve
                          * worker forwards them to). Pass NULL/0/NULL to skip. */
                         CBMLSPDef *all_defs, int def_count, char *const *def_modules,
                         /* Optional inverted index module_qn → defs[] — fallback
                          * path when there's no pre-built registry for this lang. */
                         struct CBMModuleDefIndex *module_def_index,
                         /* Optional Tier 2 full: pre-built per-language registries.
                          * For each language with a non-NULL entry, workers use the
                          * pmm_run_X_lsp_cross_with_registry fast path (skip per-
                          * file registry build entirely). Falls back to the filter
                          * + per-file build path when entry is NULL or struct is NULL.
                          * Typed as void* here to dodge the typedef/tag ordering
                          * problem — pass_parallel.c casts back to CBMCrossLspRegistries*. */
                         void *cross_registries);

/* Post-merge: create Route nodes for HTTP_CALLS/ASYNC_CALLS edges that
 * have url_path in properties but point to library functions instead of routes.
 * Re-targets these edges to Route nodes for cross-service traversal. */
void pmm_pipeline_create_route_nodes(pmm_gbuf_t *gb);

/* ── Pass function prototypes ────────────────────────────────────── */

int pmm_pipeline_pass_definitions(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files,
                                  int file_count);

int pmm_pipeline_pass_k8s(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count);

int pmm_pipeline_pass_calls(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count);

/* Cross-file LSP type-aware call resolution pass. Augments per-file
 * resolved_calls with cross-file resolutions before call edges are emitted.
 * Implementation: src/pipeline/pass_lsp_cross.c. */
int pmm_pipeline_pass_lsp_cross(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files,
                                int file_count, CBMFileResult **cache);

/* Sub-passes called from pass_calls: pattern-based edge extraction */
void pmm_pipeline_pass_fastapi_depends(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files,
                                       int file_count);

int pmm_pipeline_pass_usages(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count);

int pmm_pipeline_pass_semantic(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files,
                               int file_count);

int pmm_pipeline_pass_tests(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count);

int pmm_pipeline_pass_githistory(pmm_pipeline_ctx_t *ctx);

/* Pre-computed git history result for fused post-pass parallelism. */
typedef struct {
    pmm_change_coupling_t *couplings;
    int count;
    int commit_count;
    /* Per-file temporal data (change_count + last_modified) for File nodes.
     * NULL when the history pass had no commits to analyse. */
    pmm_file_temporal_t *file_temporal;
    int file_temporal_count;
} pmm_githistory_result_t;

/* Compute change couplings without touching the graph buffer.
 * Can run on a separate thread while other passes use the gbuf. */
int pmm_pipeline_githistory_compute(const char *repo_path, pmm_githistory_result_t *result);

/* Apply pre-computed couplings to the graph buffer (main thread only). */
int pmm_pipeline_githistory_apply(pmm_pipeline_ctx_t *ctx, const pmm_githistory_result_t *result);

/* Pre-dump pass: decorator tags enrichment (operates on gbuf). */
int pmm_pipeline_pass_decorator_tags(pmm_gbuf_t *gbuf, const char *project);

/* Pre-dump pass: config ↔ code linking. */
int pmm_pipeline_pass_configlink(pmm_pipeline_ctx_t *ctx);

/* Pre-dump pass: SIMILAR_TO edges via MinHash fingerprinting. */
int pmm_pipeline_pass_similarity(pmm_pipeline_ctx_t *ctx);

/* Pre-dump pass: SEMANTICALLY_RELATED edges via algorithmic embeddings.
 * Opt-in: only runs when PMM_SEMANTIC_ENABLED=1. */
int pmm_pipeline_pass_semantic_edges(pmm_pipeline_ctx_t *ctx);

/* Pre-dump pass: interprocedural complexity propagation (Tier B).
 * Propagates per-function loop_depth along CALLS edges into a transitive
 * worst-case nested-loop estimate (transitive_loop_depth) and flags call-graph
 * cycles (recursive). Runs on the graph buffer before the dump. */
void pmm_pipeline_pass_complexity(pmm_pipeline_ctx_t *ctx);

/* ── Env URL scanner (pass_envscan.c) ────────────────────────────── */

typedef struct {
    char key[PMM_SZ_128];
    char value[PMM_SZ_512];
    char file_path[PMM_SZ_256];
} pmm_env_binding_t;

/* Scan a project directory for environment variable assignments with URL values.
 * Walks the filesystem, scans Dockerfiles, shell scripts, .env, YAML, TOML,
 * Terraform, and .properties files. Filters out secrets.
 * Returns number of bindings written to out (up to max_out).
 * NOTE: this walker currently has no production callers — it is exercised
 * only by tests. The _excluded variant honors discovery exclusions for
 * consistency with the pkgmap/path-alias walks (#792); the plain variant
 * scans unexcluded (NULL exclusion list). */
int pmm_scan_project_env_urls(const char *root_path, pmm_env_binding_t *out, int max_out);
int pmm_scan_project_env_urls_excluded(const char *root_path, pmm_env_binding_t *out, int max_out,
                                       char **excluded_dirs, int excluded_count);

/* ── Incremental pipeline (pipeline_incremental.c) ───────────────── */

/* Run incremental re-index on an existing disk DB.
 * Classifies files by mtime+size, deletes changed nodes, re-parses changed
 * files, merges into disk DB. Returns 0 on success. */
int pmm_pipeline_run_incremental(pmm_pipeline_t *p, const char *db_path, pmm_file_info_t *files,
                                 int file_count);

/* Pipeline accessors for incremental use */
const char *pmm_pipeline_repo_path(const pmm_pipeline_t *p);
atomic_int *pmm_pipeline_cancelled_ptr(pmm_pipeline_t *p);
/* Record committed graph size (#334 gate axis) from the incremental path,
 * which cannot see the opaque pmm_pipeline struct. Call before the dump. */
void pmm_pipeline_set_committed_counts(pmm_pipeline_t *p, int nodes, int edges);

/* Test seam: invoked after a complete staging DB is sealed and immediately
 * before the cancellation check + atomic replace. Not part of the public API. */
void pmm_pipeline_set_before_publish_hook_for_tests(
    pmm_pipeline_t *p, void (*hook)(pmm_pipeline_t *, const char *, void *), void *ctx);
void pmm_pipeline_set_rename_hook_for_tests(pmm_pipeline_t *p,
                                            int (*hook)(const char *, const char *, void *),
                                            void *ctx);

/* Synchronous thread-local seam for deterministic cross-repo cancellation
 * tests. The callback runs immediately after a CROSS_* edge is committed and
 * is never retained; it must not re-enter cross-repo matching. */
typedef void (*pmm_cross_repo_after_insert_test_hook_t)(const char *project, const char *edge_type,
                                                        void *context);
void pmm_cross_repo_set_after_insert_hook_for_tests(pmm_cross_repo_after_insert_test_hook_t hook,
                                                    void *context);

/* Parse a gRPC stub call "<service-stub>.<method>" into the canonical proto
 * service name + method. Returns true ONLY when a recognized gRPC stub/client
 * suffix is present (the stub-type signal that gates Route emission, #294).
 * Exposed for testing. */
bool extract_grpc_service_method(const char *callee, char *service, size_t srv_sz, char *method,
                                 size_t meth_sz);

/* Extraction back-pressure observability (pass_parallel.c): nap-cycle counter
 * for the over-budget collect+nap gate. Test hook — asserts the gate stops
 * re-paying the nap tax once a full cycle failed to reclaim under budget
 * (futile: the resident floor, not transients, holds the memory). */
long pmm_pp_bp_nap_cycles(void);
void pmm_pp_bp_nap_cycles_reset(void);

#endif /* PMM_PIPELINE_INTERNAL_H */
