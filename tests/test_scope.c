/*
 * test_scope.c — Tests for the chunked linked-frame CBMScope.
 *
 * Phase 0 of Python LSP integration: replaces the legacy fixed 64-binding
 * array with growable per-scope chunks. Tests verify dynamic growth,
 * binding-shadowing semantics, and the parent-chain lookup contract that
 * Go and C/C++ LSP implementations rely on.
 */
#include "test_framework.h"
#include "pmm.h"
#include "lsp/scope.h"
#include "lsp/type_rep.h"

/* Build a NAMED type for a fixture string. Arena-allocated. */
static const CBMType *named_t(CBMArena *a, const char *qn) {
    return pmm_type_named(a, qn);
}

/* ── Basic API ─────────────────────────────────────────────────── */

TEST(scope_push_returns_distinct_scope) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *root = pmm_scope_push(&a, NULL);
    CBMScope *child = pmm_scope_push(&a, root);
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(child);
    ASSERT(child != root);
    ASSERT(child->parent == root);
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_pop_returns_parent) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *root = pmm_scope_push(&a, NULL);
    CBMScope *child = pmm_scope_push(&a, root);
    ASSERT(pmm_scope_pop(child) == root);
    ASSERT(pmm_scope_pop(root) == NULL);
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_lookup_unbound_returns_unknown) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    const CBMType *t = pmm_scope_lookup(s, "missing");
    ASSERT_NOT_NULL(t);
    ASSERT(pmm_type_is_unknown(t));
    pmm_arena_destroy(&a);
    PASS();
}

/* ── Binding semantics ─────────────────────────────────────────── */

TEST(scope_bind_then_lookup) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    pmm_scope_bind(s, "x", named_t(&a, "int"));
    const CBMType *t = pmm_scope_lookup(s, "x");
    ASSERT_NOT_NULL(t);
    ASSERT(t->kind == PMM_TYPE_NAMED);
    ASSERT_STR_EQ(t->data.named.qualified_name, "int");
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_rebind_overwrites_in_place) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    pmm_scope_bind(s, "x", named_t(&a, "int"));
    pmm_scope_bind(s, "x", named_t(&a, "string"));
    const CBMType *t = pmm_scope_lookup(s, "x");
    ASSERT_STR_EQ(t->data.named.qualified_name, "string");
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_lookup_walks_parent_chain) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *root = pmm_scope_push(&a, NULL);
    CBMScope *child = pmm_scope_push(&a, root);
    pmm_scope_bind(root, "outer", named_t(&a, "Outer"));
    pmm_scope_bind(child, "inner", named_t(&a, "Inner"));
    const CBMType *outer_in_child = pmm_scope_lookup(child, "outer");
    const CBMType *inner_in_root = pmm_scope_lookup(root, "inner");
    ASSERT_STR_EQ(outer_in_child->data.named.qualified_name, "Outer");
    ASSERT(pmm_type_is_unknown(inner_in_root));
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_child_shadows_parent) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *root = pmm_scope_push(&a, NULL);
    CBMScope *child = pmm_scope_push(&a, root);
    pmm_scope_bind(root, "x", named_t(&a, "ParentInt"));
    pmm_scope_bind(child, "x", named_t(&a, "ChildStr"));
    const CBMType *t = pmm_scope_lookup(child, "x");
    ASSERT_STR_EQ(t->data.named.qualified_name, "ChildStr");
    pmm_arena_destroy(&a);
    PASS();
}

/* ── Dynamic growth — the Phase 0 win ───────────────────────────── */

TEST(scope_dynamic_growth_300_bindings) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    char names[300][16];
    for (int i = 0; i < 300; i++) {
        snprintf(names[i], sizeof(names[0]), "v%d", i);
        char qn[32];
        snprintf(qn, sizeof(qn), "T%d", i);
        pmm_scope_bind(s, names[i], named_t(&a, qn));
    }
    /* All 300 bindings retrievable — old 64-cap would have dropped 236 */
    for (int i = 0; i < 300; i++) {
        const CBMType *t = pmm_scope_lookup(s, names[i]);
        ASSERT_NOT_NULL(t);
        ASSERT(t->kind == PMM_TYPE_NAMED);
        char expected[32];
        snprintf(expected, sizeof(expected), "T%d", i);
        ASSERT_STR_EQ(t->data.named.qualified_name, expected);
    }
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_growth_chunk_boundary) {
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    char names[PMM_SCOPE_CHUNK_BINDINGS + 1][16];
    /* Fill exactly one chunk + spillover: forces second chunk allocation */
    for (int i = 0; i <= PMM_SCOPE_CHUNK_BINDINGS; i++) {
        snprintf(names[i], sizeof(names[0]), "n%d", i);
        char qn[16];
        snprintf(qn, sizeof(qn), "T%d", i);
        pmm_scope_bind(s, names[i], named_t(&a, qn));
    }
    /* First-chunk binding still retrievable */
    const CBMType *first = pmm_scope_lookup(s, "n0");
    ASSERT_STR_EQ(first->data.named.qualified_name, "T0");
    /* Spillover binding (would have fallen off legacy 64-cap if cap < 17) */
    char last_name[16];
    snprintf(last_name, sizeof(last_name), "n%d", PMM_SCOPE_CHUNK_BINDINGS);
    char last_qn[16];
    snprintf(last_qn, sizeof(last_qn), "T%d", PMM_SCOPE_CHUNK_BINDINGS);
    const CBMType *last = pmm_scope_lookup(s, last_name);
    ASSERT_STR_EQ(last->data.named.qualified_name, last_qn);
    pmm_arena_destroy(&a);
    PASS();
}

TEST(scope_rebind_in_old_chunk) {
    /* After many bindings have spilled into newer chunks, rebinding a name
     * from the original chunk must overwrite that binding in place rather
     * than create a duplicate in the head chunk. */
    CBMArena a;
    pmm_arena_init(&a);
    CBMScope *s = pmm_scope_push(&a, NULL);
    pmm_scope_bind(s, "early", named_t(&a, "EarlyV1"));
    char names[100][16];
    for (int i = 0; i < 100; i++) {
        snprintf(names[i], sizeof(names[0]), "fill%d", i);
        pmm_scope_bind(s, names[i], named_t(&a, "Filler"));
    }
    pmm_scope_bind(s, "early", named_t(&a, "EarlyV2"));
    const CBMType *t = pmm_scope_lookup(s, "early");
    ASSERT_STR_EQ(t->data.named.qualified_name, "EarlyV2");
    pmm_arena_destroy(&a);
    PASS();
}

/* ── Suite registration ────────────────────────────────────────── */

SUITE(scope) {
    RUN_TEST(scope_push_returns_distinct_scope);
    RUN_TEST(scope_pop_returns_parent);
    RUN_TEST(scope_lookup_unbound_returns_unknown);
    RUN_TEST(scope_bind_then_lookup);
    RUN_TEST(scope_rebind_overwrites_in_place);
    RUN_TEST(scope_lookup_walks_parent_chain);
    RUN_TEST(scope_child_shadows_parent);
    RUN_TEST(scope_dynamic_growth_300_bindings);
    RUN_TEST(scope_growth_chunk_boundary);
    RUN_TEST(scope_rebind_in_old_chunk);
}
