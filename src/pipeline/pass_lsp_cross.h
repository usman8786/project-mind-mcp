/*
 * pass_lsp_cross.h — Cross-file LSP helpers shared with the parallel
 * resolve pass.
 *
 * Per-file LSP (pmm_run_X_lsp inside pmm_extract_file) only sees a single
 * file's defs in its registry, so callees whose receiver type comes from
 * an imported module stay unresolved. The helpers declared here close
 * that gap: they let the parallel resolve worker (pass_parallel.c) build
 * a project-wide CBMLSPDef[] and invoke the language-specific
 * pmm_run_X_lsp_cross resolver on each file using the file's already-
 * built import map. Resolved calls are appended to result->resolved_calls
 * so the same pmm_pipeline_find_lsp_resolution path that handles per-
 * file LSP picks them up.
 *
 * Languages covered: Go, C/C++/CUDA, Python, TypeScript/JavaScript/JSX/
 * TSX, PHP, C#, and JVM (Java/Kotlin via the shared filter helper).
 * Anything else short-circuits via pmm_pxc_has_cross_lsp.
 *
 * Previously this work ran as a separate sequential pipeline pass
 * (pmm_pipeline_pass_lsp_cross) that re-read every source file from
 * disk and re-parsed each tree-sitter tree on a single thread — a 50×
 * regression vs the parallel extract pass on large repos. The pass was
 * deleted; the resolve worker now invokes these helpers directly using
 * the source bytes retained in result->arena during extract.
 */
#ifndef PMM_PIPELINE_PASS_LSP_CROSS_H
#define PMM_PIPELINE_PASS_LSP_CROSS_H

#include "pmm.h"
/* CBMLSPDef historically lives in lsp/go_lsp.h (not lsp/type_rep.h)
 * — type_rep.h covers the type-representation primitives while
 * go_lsp.h was where the project-wide def descriptor landed first. */
#include "lsp/go_lsp.h"
#include "lsp/py_lsp.h"   /* pmm_py_build_cross_registry / pmm_run_py_lsp_cross_with_registry */
#include "lsp/c_lsp.h"    /* pmm_c_build_cross_registry / pmm_run_c_lsp_cross_with_registry */
#include "lsp/cs_lsp.h"   /* pmm_cs_build_cross_registry / pmm_run_cs_lsp_cross_with_registry */
#include "lsp/ts_lsp.h"   /* pmm_ts_build_cross_registry / pmm_run_ts_lsp_cross_with_registry */
#include "lsp/rust_lsp.h" /* pmm_rust_build_cross_registry / pmm_run_rust_lsp_cross_with_registry */
#include "pipeline/pipeline_internal.h"
#include <stdbool.h>

/* True iff this language has a pmm_run_X_lsp_cross resolver wired up. */
bool pmm_pxc_has_cross_lsp(CBMLanguage lang);

/* Collect a project-wide CBMLSPDef[] from every cached file result.
 * def_modules[i] receives the module QN for files[i] (malloc'd; the
 * caller frees each entry then the array). String fields in the
 * returned CBMLSPDef[] are borrowed from cache[i]->arena and from
 * def_modules[i] — caller must keep both alive while the array is in
 * use. Returns the malloc'd array (free() it) and writes the entry
 * count to *out_count. Returns NULL on alloc failure or when no defs
 * exist. */
CBMLSPDef *pmm_pxc_collect_all_defs(CBMFileResult **cache, const pmm_file_info_t *files,
                                    int file_count, const char *project_name, char **def_modules,
                                    int *out_count);

/* Detect TS dialect flags from a relative path. */
void pmm_pxc_ts_modes(CBMLanguage lang, const char *rel_path, bool *out_js, bool *out_jsx,
                      bool *out_dts);

/* ── Per-module def index (the gopls "package summary" pattern) ──
 *
 * The hot path used to register ALL all_defs[] into a fresh registry
 * per file (~110k defs × 11k files for kubernetes = ~21,000 CPU-s of
 * arena_strdup). Most of those defs are irrelevant to any one file —
 * each file only references defs from its own module + its imported
 * modules. gopls observed the same: it builds per-package summaries
 * and per-file only loads the summaries the file imports.
 *
 * pmm_pxc_build_module_def_index() builds inverted indexes once (O(D)):
 * def_module_qn → defs and declared namespace/package → defs.
 * pmm_pxc_filter_defs_for_file() then returns own_module + imp_qns for
 * most languages. For Java/Kotlin callers it additionally returns
 * same-namespace JVM defs so Gradle/Maven mixed source roots
 * (`src/main/java/...` + `src/main/kotlin/...`) resolve same-package
 * references without falling back to a full project registry per file. */
typedef struct CBMModuleDefIndex CBMModuleDefIndex;

CBMModuleDefIndex *pmm_pxc_build_module_def_index(CBMLSPDef *all_defs, int def_count);

void pmm_pxc_free_module_def_index(CBMModuleDefIndex *idx);

/* Return a malloc'd CBMLSPDef[] containing all defs whose
 * def_module_qn matches own_module OR any of imp_qns. For Java/Kotlin
 * callers, also include defs from the same declared package/namespace:
 * JVM same-package references often cross `src/main/java` and
 * `src/main/kotlin` roots without import statements. String fields inside
 * each entry are borrowed from the original all_defs[] arena (caller keeps
 * it alive). Caller frees the returned array with free(). Writes the entry
 * count to *out_count. Returns NULL if no matches (with *out_count = 0). */
CBMLSPDef *pmm_pxc_filter_defs_for_file(const CBMModuleDefIndex *idx, CBMLSPDef *all_defs,
                                        CBMLanguage caller_lang, const char *caller_namespace,
                                        const char *own_module, const char *const *imp_qns,
                                        int imp_count, int *out_count);

/* ── Tier 2 full: pre-built per-language cross-LSP registries ─────
 *
 * Each non-NULL registry is built ONCE in pipeline.c (in a dedicated
 * cross_lsp_arena), finalized, and shared READ-ONLY across all
 * resolve workers for files of that language. The worker uses the
 * matching pmm_run_X_lsp_cross_with_registry variant which skips the
 * per-file registry build entirely. NULL → fall back to the per-file
 * pmm_pxc_run_one path. */
typedef struct {
    CBMTypeRegistry *go;     /* PMM_LANG_GO */
    CBMTypeRegistry *c;      /* PMM_LANG_C, PMM_LANG_CPP, PMM_LANG_CUDA */
    CBMTypeRegistry *python; /* PMM_LANG_PYTHON */
    CBMTypeRegistry *ts;     /* PMM_LANG_JAVASCRIPT, TYPESCRIPT, TSX */
    CBMTypeRegistry *php;    /* PMM_LANG_PHP */
    CBMTypeRegistry *cs;     /* PMM_LANG_CSHARP */
    /* PMM_LANG_RUST: intentionally absent — the shared rust registry is built
     * LAZILY inside pmm_parallel_resolve (first NULL-filter rust file), not eagerly. */
} CBMCrossLspRegistries;

/* Return the appropriate pre-built registry for a language, or NULL
 * if none was built (or language has no cross-LSP entrypoint). */
static inline CBMTypeRegistry *pmm_pxc_registry_for_lang(const CBMCrossLspRegistries *r,
                                                         CBMLanguage lang) {
    if (!r)
        return NULL;
    switch (lang) {
    case PMM_LANG_GO:
        return r->go;
    case PMM_LANG_C:   /* fallthrough */
    case PMM_LANG_CPP: /* fallthrough */
    case PMM_LANG_CUDA:
        return r->c;
    case PMM_LANG_PYTHON:
        return r->python;
    case PMM_LANG_JAVASCRIPT: /* fallthrough */
    case PMM_LANG_TYPESCRIPT: /* fallthrough */
    case PMM_LANG_TSX:
        return r->ts;
    case PMM_LANG_PHP:
        return r->php;
    case PMM_LANG_CSHARP:
        return r->cs;
    default:
        return NULL; /* incl. PMM_LANG_RUST — its shared registry is built lazily */
    }
}

/* Borrow the (thread-local) Rust Cargo manifest the cross-file LSP pass set for
 * cross-crate (#56) routing. The Tier-2 prebuilt Rust resolve reads it so it sees
 * exactly what the per-file fallback (pmm_pxc_run_one) would on the same thread. */
struct CBMCargoManifest;
const struct CBMCargoManifest *pmm_pxc_get_rust_manifest(void);

/* Run the cross-file LSP resolver for non-TS languages. Appends
 * resolved CALLS into r->resolved_calls (lives in r->arena). Caller
 * owns source, module_qn, all_defs, imp_keys, imp_vals.
 * NOTE: all_defs is read-only in practice but typed non-const to match
 * the existing pmm_run_X_lsp_cross callee signatures. */
void pmm_pxc_run_one(CBMLanguage lang, CBMFileResult *r, const char *source, int source_len,
                     const char *module_qn, CBMLSPDef *all_defs, int def_count,
                     const char **imp_keys, const char **imp_vals, int imp_count);

/* TS / JS / JSX / TSX variant with explicit dialect flags. */
void pmm_pxc_run_one_ts(CBMFileResult *r, const char *source, int source_len, const char *module_qn,
                        CBMLSPDef *all_defs, int def_count, const char **imp_keys,
                        const char **imp_vals, int imp_count, bool js_mode, bool jsx_mode,
                        bool dts_mode);

/* Per-file cross-LSP dispatch shared by the parallel resolve worker AND the
 * sequential driver (one path = one semantics): module-def-index filter →
 * shared prebuilt registry (overlay pattern, no per-file registry build) →
 * per-file fallback with FILTERED defs for languages without a shared
 * variant. rust_shared_get (nullable) supplies the lazily-built shared Rust
 * registry for NULL-filter rust files. */
void pmm_pxc_dispatch_file(CBMLanguage lang, CBMFileResult *result, const char *source,
                           int source_len, const char *rel, const char *def_module,
                           const CBMCrossLspRegistries *cross_registries,
                           const CBMModuleDefIndex *module_def_index, CBMLSPDef *all_defs,
                           int all_def_count, const char **imp_keys, const char **imp_vals,
                           int imp_count, CBMTypeRegistry *(*rust_shared_get)(void *),
                           void *rust_shared_ctx);

#endif /* PMM_PIPELINE_PASS_LSP_CROSS_H */
