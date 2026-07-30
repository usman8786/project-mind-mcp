#include "scope.h"
#include <string.h>

CBMScope* pmm_scope_push(CBMArena* a, CBMScope* current) {
    CBMScope* scope = (CBMScope*)pmm_arena_alloc(a, sizeof(CBMScope));
    if (!scope) {
        return current;
    }
    memset(scope, 0, sizeof(CBMScope));
    scope->parent = current;
    scope->arena = a;
    return scope;
}

CBMScope* pmm_scope_pop(CBMScope* scope) {
    if (!scope) {
        return NULL;
    }
    return scope->parent;
}

static CBMScopeChunk* alloc_chunk(CBMScope* scope) {
    if (!scope->arena) {
        return NULL;
    }
    CBMScopeChunk* c = (CBMScopeChunk*)pmm_arena_alloc(scope->arena, sizeof(CBMScopeChunk));
    if (!c) {
        return NULL;
    }
    memset(c, 0, sizeof(CBMScopeChunk));
    c->next = scope->chunks;
    scope->chunks = c;
    return c;
}

void pmm_scope_bind(CBMScope* scope, const char* name, const CBMType* type) {
    if (!scope || !name) {
        return;
    }
    for (CBMScopeChunk* c = scope->chunks; c != NULL; c = c->next) {
        for (int i = 0; i < c->used; i++) {
            if (c->bindings[i].name && strcmp(c->bindings[i].name, name) == 0) {
                c->bindings[i].type = type;
                return;
            }
        }
    }
    CBMScopeChunk* head = scope->chunks;
    if (!head || head->used >= PMM_SCOPE_CHUNK_BINDINGS) {
        head = alloc_chunk(scope);
        if (!head) {
            return;
        }
    }
    head->bindings[head->used].name = name;
    head->bindings[head->used].type = type;
    head->used++;
}

const CBMType* pmm_scope_lookup(const CBMScope* scope, const char* name) {
    if (!name) {
        return pmm_type_unknown();
    }
    for (const CBMScope* s = scope; s != NULL; s = s->parent) {
        for (CBMScopeChunk* c = s->chunks; c != NULL; c = c->next) {
            for (int i = 0; i < c->used; i++) {
                if (c->bindings[i].name && strcmp(c->bindings[i].name, name) == 0) {
                    return c->bindings[i].type;
                }
            }
        }
    }
    return pmm_type_unknown();
}
