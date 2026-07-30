/*
 * userconfig.h — User-defined file extension → language mappings.
 *
 * Reads extra_extensions from two optional JSON config files:
 *   Global:  $XDG_CONFIG_HOME/project-mind-mcp/config.json
 *            (falls back to ~/.config/project-mind-mcp/config.json)
 *   Project: {repo_root}/.project-mind-mcp.json
 *
 * Project config wins over global. Unknown language values warn and are
 * skipped (fail-open). Missing files are silently ignored.
 *
 * Format:
 *   {"extra_extensions": {".blade.php": "php", ".mjs": "javascript"}}
 *
 * The language string matching is case-insensitive.
 */
#ifndef PMM_USERCONFIG_H
#define PMM_USERCONFIG_H

#include "pmm.h" /* CBMLanguage */

/* ── Types ──────────────────────────────────────────────────────── */

typedef struct {
    char *ext;        /* file extension including dot, e.g. ".blade.php" */
    CBMLanguage lang; /* resolved language enum */
} pmm_userext_t;

typedef struct {
    pmm_userext_t *entries; /* heap-allocated array */
    int count;              /* number of entries */
} pmm_userconfig_t;

/* ── API ────────────────────────────────────────────────────────── */

/*
 * Load user config from global + project files, merge (project wins).
 * repo_path: absolute path to the repository root (for project config).
 * Returns a heap-allocated pmm_userconfig_t (caller must free via
 * pmm_userconfig_free). Returns NULL only on allocation failure.
 * Missing config files are silently ignored.
 */
pmm_userconfig_t *pmm_userconfig_load(const char *repo_path);

/*
 * Look up a file extension in the user config.
 * ext: extension including dot, e.g. ".blade.php"
 * Returns the mapped CBMLanguage, or PMM_LANG_COUNT if not found.
 */
CBMLanguage pmm_userconfig_lookup(const pmm_userconfig_t *cfg, const char *ext);

/* Free a pmm_userconfig_t returned by pmm_userconfig_load. NULL-safe. */
void pmm_userconfig_free(pmm_userconfig_t *cfg);

/* ── Integration hook ───────────────────────────────────────────── */

/*
 * Set the process-global user config that pmm_language_for_extension()
 * will consult before the built-in table.
 * cfg may be NULL to clear the override.
 * Not thread-safe — call before spawning worker threads.
 */
void pmm_set_user_lang_config(const pmm_userconfig_t *cfg);

/*
 * Get the currently active process-global user config.
 * Returns NULL if none has been set.
 * Called internally by pmm_language_for_extension().
 */
const pmm_userconfig_t *pmm_get_user_lang_config(void);

#endif /* PMM_USERCONFIG_H */
