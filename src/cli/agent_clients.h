/*
 * agent_clients.h — Table-driven agent client MCP installation profiles.
 */
#ifndef PMM_CLI_AGENT_CLIENTS_H
#define PMM_CLI_AGENT_CLIENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PMM_AGENT_CLIENT_QODER = 0,
    PMM_AGENT_CLIENT_KIMI,
    PMM_AGENT_CLIENT_GITLAB_DUO,
    PMM_AGENT_CLIENT_ROVO_DEV,
    PMM_AGENT_CLIENT_AMP,
    PMM_AGENT_CLIENT_DEVIN,
    PMM_AGENT_CLIENT_TABNINE,
    PMM_AGENT_CLIENT_CONTINUE,
    PMM_AGENT_CLIENT_VISUAL_STUDIO,
    PMM_AGENT_CLIENT_TRAE,
    PMM_AGENT_CLIENT_ROO_CODE,
    PMM_AGENT_CLIENT_AMAZON_Q,
    PMM_AGENT_CLIENT_CODEBUDDY,
    PMM_AGENT_CLIENT_IBM_BOB_IDE,
    PMM_AGENT_CLIENT_IBM_BOB_SHELL,
    PMM_AGENT_CLIENT_POCHI,
    PMM_AGENT_CLIENT_PI,
    PMM_AGENT_CLIENT_SOURCEGRAPH_CODY,
    PMM_AGENT_CLIENT_COUNT
} pmm_agent_client_id_t;

typedef enum {
    PMM_AGENT_STABLE = 0,
    PMM_AGENT_CONDITIONAL,
    PMM_AGENT_OPT_IN
} pmm_agent_client_stability_t;

enum {
    PMM_AGENT_CAP_MCP = UINT32_C(1) << 0,
    PMM_AGENT_CAP_INSTRUCTIONS = UINT32_C(1) << 1,
    PMM_AGENT_CAP_SKILL = UINT32_C(1) << 2,
    PMM_AGENT_CAP_AGENT = UINT32_C(1) << 3,
    PMM_AGENT_CAP_HOOK = UINT32_C(1) << 4,
    PMM_AGENT_CAP_PLUGIN = UINT32_C(1) << 5
};

typedef int (*pmm_agent_mcp_edit_fn)(pmm_agent_client_id_t id, const char *config_path,
                                     const char *binary_path);

typedef struct {
    pmm_agent_client_id_t id;
    const char *stable_id;
    const char *display_name;
    pmm_agent_client_stability_t stability;
    uint32_t capabilities;
    const char *detection_command;
    pmm_agent_mcp_edit_fn install_mcp;
    pmm_agent_mcp_edit_fn remove_mcp;
} pmm_agent_client_profile_t;

typedef bool (*pmm_agent_probe_fn)(const char *value, const void *context);

typedef struct {
    const char *home_dir;
    const char *xdg_config_home;
    const char *appdata_dir;
    const char *glab_config_dir;
    const char *kimi_code_home;
    const char *continue_config_path;
    const char *trae_config_path;
    const char *roo_config_path;
    const char *cody_config_path;
    bool is_windows;
    pmm_agent_probe_fn path_exists;
    pmm_agent_probe_fn command_exists;
    const void *probe_context;
} pmm_agent_client_resolve_options_t;

enum {
    PMM_AGENT_EDIT_ERROR = -1,
    PMM_AGENT_EDIT_OK = 0,
    PMM_AGENT_EDIT_FOREIGN = 1,
    PMM_AGENT_EDIT_NOT_APPLICABLE = 2
};

size_t pmm_agent_client_count(void);
const pmm_agent_client_profile_t *pmm_agent_client_at(size_t index);
const pmm_agent_client_profile_t *pmm_agent_client_by_id(pmm_agent_client_id_t id);
const pmm_agent_client_profile_t *pmm_agent_client_by_stable_id(const char *stable_id);

/* Resolves the documented user config path. Returns 0 on success, 1 when a
 * conditional target has no safe active path, and -1 for invalid input or an
 * ambiguous/unsupported configuration. */
int pmm_agent_client_resolve_path(pmm_agent_client_id_t id,
                                  const pmm_agent_client_resolve_options_t *options, char *path_out,
                                  size_t path_out_size);
bool pmm_agent_client_detect(pmm_agent_client_id_t id,
                             const pmm_agent_client_resolve_options_t *options);
bool pmm_agent_client_cleanup_candidate(pmm_agent_client_id_t id,
                                        const pmm_agent_client_resolve_options_t *options);

/* config_path must already have been resolved. The adapter never guesses a
 * target here. Existing same-name foreign entries fail closed with
 * PMM_AGENT_EDIT_FOREIGN. Removal requires the original installed binary path
 * and only removes the still-canonical entry. */
int pmm_agent_client_install_mcp(pmm_agent_client_id_t id, const char *config_path,
                                 const char *binary_path);
int pmm_agent_client_remove_mcp(pmm_agent_client_id_t id, const char *config_path,
                                const char *binary_path);

#ifdef __cplusplus
}
#endif

#endif /* PMM_CLI_AGENT_CLIENTS_H */
