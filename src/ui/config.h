/*
 * config.h — Persistent UI configuration.
 *
 * Stores ui_enabled and ui_port in ~/.cache/project-mind-mcp/config.json.
 * Thread-safe: load/save are independent operations on the filesystem.
 */
#ifndef PMM_UI_CONFIG_H
#define PMM_UI_CONFIG_H

#include <stdbool.h>

/* Default values */
#define PMM_UI_DEFAULT_PORT 9749
#define PMM_UI_DEFAULT_ENABLED false

typedef struct {
    bool ui_enabled;
    int ui_port;
} pmm_ui_config_t;

/* Load config from disk. Missing/corrupt file → defaults. */
void pmm_ui_config_load(pmm_ui_config_t *cfg);

/* Atomically save one complete config generation. Creates the directory if
 * needed and reports write/sync/replace failures. */
bool pmm_ui_config_save(const pmm_ui_config_t *cfg);

/* Get the config file path. Writes to buf (up to bufsz bytes).
 * Exposed for testing. */
void pmm_ui_config_path(char *buf, int bufsz);

#endif /* PMM_UI_CONFIG_H */
