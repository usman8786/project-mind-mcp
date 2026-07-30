#ifndef PMM_LSP_SCOPE_H
#define PMM_LSP_SCOPE_H

#include "type_rep.h"
#include "../arena.h"
#include <stdatomic.h> /* relaxed cache for pmm_lsp_max_walk_depth */
#include <stdlib.h>     /* getenv, atoi (pmm_lsp_max_walk_depth) */

typedef struct {
    const char* name;
    const CBMType* type;
} CBMVarBinding;

#define PMM_SCOPE_CHUNK_BINDINGS 16

typedef struct CBMScopeChunk {
    CBMVarBinding bindings[PMM_SCOPE_CHUNK_BINDINGS];
    int used;
    struct CBMScopeChunk* next;
} CBMScopeChunk;

typedef struct CBMScope {
    struct CBMScope* parent;
    CBMScopeChunk* chunks;
    CBMArena* arena;        // owning arena, propagated to children at push time
} CBMScope;

// Bail-to-UNKNOWN depth for type-lookup chains: alias resolution, MRO walks,
// embedded-field/struct-traversal. Exceeding this collapses to pmm_type_unknown
// rather than recursing — guards against pathological hierarchies.
#define PMM_LSP_MAX_LOOKUP_DEPTH 16

// Recursion cap for the per-language "resolve calls in AST node" walkers. These
// recurse once per AST nesting level; a deeply-nested or cyclic file can drive
// them into a native stack overflow (SIGSEGV) that takes down the whole index.
// Past this cap the wrapper skips the subtree — those calls stay unresolved,
// which is graceful degradation, not a crash. 512 is far deeper than any
// hand-written source nests; override for pathological/generated repos via the
// PMM_LSP_MAX_WALK_DEPTH env var (positive integer).
#define PMM_LSP_MAX_WALK_DEPTH 512

// Resolved walk-depth cap: env override (PMM_LSP_MAX_WALK_DEPTH, if a positive
// integer) else PMM_LSP_MAX_WALK_DEPTH. Read once and cached — the walkers call
// this per node, so it must not hit getenv on the hot path. The cache is
// idempotent under multi-threaded indexing (every worker computes the same
// value), but a plain data race is undefined behavior even when the values
// agree, so the slot is a relaxed atomic: on the hot path this is a plain load
// with no fence, and a first-touch double-compute simply stores the same
// value. This keeps the parallel extractor TSan-clean.
static inline int pmm_lsp_max_walk_depth(void) {
    static _Atomic int cached = -1;
    int value = atomic_load_explicit(&cached, memory_order_relaxed);
    if (value < 0) {
        const char* e = getenv("PMM_LSP_MAX_WALK_DEPTH");
        int v = (e && *e) ? atoi(e) : 0;
        value = (v > 0) ? v : PMM_LSP_MAX_WALK_DEPTH;
        atomic_store_explicit(&cached, value, memory_order_relaxed);
    }
    return value;
}

CBMScope* pmm_scope_push(CBMArena* a, CBMScope* current);
CBMScope* pmm_scope_pop(CBMScope* scope);
void pmm_scope_bind(CBMScope* scope, const char* name, const CBMType* type);
const CBMType* pmm_scope_lookup(const CBMScope* scope, const char* name);

#endif // PMM_LSP_SCOPE_H
