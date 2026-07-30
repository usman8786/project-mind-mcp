/*
 * agent_profiles.h — Canonical tiered project-mind agent profiles.
 */
#ifndef PMM_CLI_AGENT_PROFILES_H
#define PMM_CLI_AGENT_PROFILES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PMM_GRAPH_TIER_SCOUT = 0,
    PMM_GRAPH_TIER_VERIFY,
    PMM_GRAPH_TIER_AUDIT,
    PMM_GRAPH_TIER_COUNT
} pmm_graph_tier_t;

typedef enum {
    PMM_GRAPH_ACCESS_DIRECT = 0,
    PMM_GRAPH_ACCESS_HANDOFF,
    PMM_GRAPH_ACCESS_COUNT
} pmm_graph_access_t;

typedef enum {
    PMM_GRAPH_DIALECT_CLAUDE = 0,
    PMM_GRAPH_DIALECT_CODEX,
    PMM_GRAPH_DIALECT_GEMINI,
    PMM_GRAPH_DIALECT_QWEN,
    PMM_GRAPH_DIALECT_COPILOT,
    PMM_GRAPH_DIALECT_OPENCODE,
    PMM_GRAPH_DIALECT_KILO,
    PMM_GRAPH_DIALECT_KIRO,
    PMM_GRAPH_DIALECT_JUNIE,
    PMM_GRAPH_DIALECT_QODER,
    PMM_GRAPH_DIALECT_CODEBUDDY,
    PMM_GRAPH_DIALECT_FACTORY,
    PMM_GRAPH_DIALECT_VIBE,
    PMM_GRAPH_DIALECT_AUGMENT,
    PMM_GRAPH_DIALECT_CURSOR,
    PMM_GRAPH_DIALECT_ROVO,
    PMM_GRAPH_DIALECT_POCHI,
    PMM_GRAPH_DIALECT_COUNT
} pmm_graph_profile_dialect_t;

/* Stable profile identifier. VERIFY intentionally retains "project-mind". */
const char *pmm_graph_tier_slug(pmm_graph_tier_t tier);
const char *pmm_graph_tier_display_name(pmm_graph_tier_t tier);
bool pmm_graph_dialect_direct_capable(pmm_graph_profile_dialect_t dialect);

/* Returns malloc-owned profile content, or NULL for invalid/unsafe combinations.
 * binary_path is required for a direct Kiro profile and ignored otherwise. */
char *pmm_render_graph_profile(pmm_graph_profile_dialect_t dialect, pmm_graph_tier_t tier,
                               pmm_graph_access_t access, const char *binary_path);

/* Vibe stores the behavioral prompt separately from its TOML agent definition.
 * Other integrations may also use this as the canonical contract text. */
char *pmm_render_graph_prompt(pmm_graph_tier_t tier, pmm_graph_access_t access);

#ifdef __cplusplus
}
#endif

#endif /* PMM_CLI_AGENT_PROFILES_H */
