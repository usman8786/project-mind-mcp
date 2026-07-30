/*
 * userconfig.c — User-defined extension→language mappings.
 *
 * Reads extra_extensions from:
 *   Global:  $XDG_CONFIG_HOME/project-mind-mcp/config.json
 *            (falls back to ~/.config/project-mind-mcp/config.json)
 *   Project: {repo_root}/.project-mind-mcp.json
 *
 * Project config wins over global. Unknown language values warn and are
 * skipped (fail-open). Missing files are silently ignored.
 */
#include "discover/userconfig.h"
#include "pmm.h" /* CBMLanguage, PMM_LANG_* */
#include "foundation/constants.h"
#include "foundation/platform.h" /* pmm_safe_getenv */
#include "foundation/compat_fs.h"

enum { MAX_CONFIG_SIZE = 65536 };
#include "foundation/log.h"

#include <yyjson/yyjson.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Process-global user config pointer ──────────────────────────── */

static const pmm_userconfig_t *g_userconfig = NULL;

void pmm_set_user_lang_config(const pmm_userconfig_t *cfg) {
    g_userconfig = cfg;
}

const pmm_userconfig_t *pmm_get_user_lang_config(void) {
    return g_userconfig;
}

/* ── Language name → enum table ──────────────────────────────────── */

/*
 * Reverse-mapping from lowercase language name strings to CBMLanguage.
 * Covers all names exposed by pmm_language_name() plus common aliases.
 */
typedef struct {
    const char *name; /* lowercase */
    CBMLanguage lang;
} lang_name_entry_t;

static const lang_name_entry_t LANG_NAME_TABLE[] = {
    {"go", PMM_LANG_GO},
    {"python", PMM_LANG_PYTHON},
    {"javascript", PMM_LANG_JAVASCRIPT},
    {"typescript", PMM_LANG_TYPESCRIPT},
    {"tsx", PMM_LANG_TSX},
    {"rust", PMM_LANG_RUST},
    {"java", PMM_LANG_JAVA},
    {"c++", PMM_LANG_CPP},
    {"cpp", PMM_LANG_CPP},
    {"c#", PMM_LANG_CSHARP},
    {"csharp", PMM_LANG_CSHARP},
    {"php", PMM_LANG_PHP},
    {"lua", PMM_LANG_LUA},
    {"scala", PMM_LANG_SCALA},
    {"kotlin", PMM_LANG_KOTLIN},
    {"ruby", PMM_LANG_RUBY},
    {"c", PMM_LANG_C},
    {"bash", PMM_LANG_BASH},
    {"sh", PMM_LANG_BASH},
    {"zig", PMM_LANG_ZIG},
    {"elixir", PMM_LANG_ELIXIR},
    {"haskell", PMM_LANG_HASKELL},
    {"ocaml", PMM_LANG_OCAML},
    {"objective-c", PMM_LANG_OBJC},
    {"objc", PMM_LANG_OBJC},
    {"swift", PMM_LANG_SWIFT},
    {"dart", PMM_LANG_DART},
    {"perl", PMM_LANG_PERL},
    {"groovy", PMM_LANG_GROOVY},
    {"erlang", PMM_LANG_ERLANG},
    {"r", PMM_LANG_R},
    {"html", PMM_LANG_HTML},
    {"css", PMM_LANG_CSS},
    {"scss", PMM_LANG_SCSS},
    {"yaml", PMM_LANG_YAML},
    {"toml", PMM_LANG_TOML},
    {"hcl", PMM_LANG_HCL},
    {"terraform", PMM_LANG_HCL},
    {"sql", PMM_LANG_SQL},
    {"dockerfile", PMM_LANG_DOCKERFILE},
    {"clojure", PMM_LANG_CLOJURE},
    {"f#", PMM_LANG_FSHARP},
    {"fsharp", PMM_LANG_FSHARP},
    {"julia", PMM_LANG_JULIA},
    {"vimscript", PMM_LANG_VIMSCRIPT},
    {"nix", PMM_LANG_NIX},
    {"common lisp", PMM_LANG_COMMONLISP},
    {"commonlisp", PMM_LANG_COMMONLISP},
    {"lisp", PMM_LANG_COMMONLISP},
    {"elm", PMM_LANG_ELM},
    {"fortran", PMM_LANG_FORTRAN},
    {"cuda", PMM_LANG_CUDA},
    {"cobol", PMM_LANG_COBOL},
    {"verilog", PMM_LANG_VERILOG},
    {"emacs lisp", PMM_LANG_EMACSLISP},
    {"emacslisp", PMM_LANG_EMACSLISP},
    {"json", PMM_LANG_JSON},
    {"xml", PMM_LANG_XML},
    {"markdown", PMM_LANG_MARKDOWN},
    {"makefile", PMM_LANG_MAKEFILE},
    {"cmake", PMM_LANG_CMAKE},
    {"protobuf", PMM_LANG_PROTOBUF},
    {"graphql", PMM_LANG_GRAPHQL},
    {"vue", PMM_LANG_VUE},
    {"svelte", PMM_LANG_SVELTE},
    {"meson", PMM_LANG_MESON},
    {"glsl", PMM_LANG_GLSL},
    {"ini", PMM_LANG_INI},
    {"matlab", PMM_LANG_MATLAB},
    {"mojo", PMM_LANG_MOJO},
    {"lean", PMM_LANG_LEAN},
    {"form", PMM_LANG_FORM},
    {"magma", PMM_LANG_MAGMA},
    {"wolfram", PMM_LANG_WOLFRAM},
};

#define LANG_NAME_TABLE_SIZE (sizeof(LANG_NAME_TABLE) / sizeof(LANG_NAME_TABLE[0]))

/*
 * Parse a language string (case-insensitive) to a CBMLanguage enum.
 * Returns PMM_LANG_COUNT if the string is not recognized.
 */
static CBMLanguage lang_from_string(const char *s) {
    if (!s || !s[0]) {
        return PMM_LANG_COUNT;
    }

    /* Build a lowercase copy for comparison */
    char lower[PMM_SZ_64];
    size_t i;
    for (i = 0; i < sizeof(lower) - SKIP_ONE && s[i]; i++) {
        lower[i] = (char)tolower((unsigned char)s[i]);
    }
    lower[i] = '\0';

    for (size_t j = 0; j < LANG_NAME_TABLE_SIZE; j++) {
        if (strcmp(LANG_NAME_TABLE[j].name, lower) == 0) {
            return LANG_NAME_TABLE[j].lang;
        }
    }
    return PMM_LANG_COUNT;
}

/* ── Config directory helper ─────────────────────────────────────── */

/* pmm_app_config_dir() is now in platform.c (cross-platform). */

/* ── JSON parsing ────────────────────────────────────────────────── */

/*
 * Parse extra_extensions from a yyjson object root.
 * Appends valid entries to *entries / *count (growing via realloc).
 * Project-level entries (from_project=true) are appended after global
 * entries so that a later dedup pass can prefer project values.
 *
 * Returns 0 on success, -1 on alloc failure.
 */
static int parse_extra_extensions(yyjson_val *root, pmm_userext_t **entries, int *count,
                                  const char *source_label) {
    if (!yyjson_is_obj(root)) {
        pmm_log_warn("userconfig.bad_root", "file", source_label);
        return 0;
    }

    yyjson_val *extra = yyjson_obj_get(root, "extra_extensions");
    if (!extra) {
        return 0; /* key absent — fine */
    }
    if (!yyjson_is_obj(extra)) {
        pmm_log_warn("userconfig.bad_extra_extensions", "file", source_label);
        return 0;
    }

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(extra, &iter);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val *val = yyjson_obj_iter_get_val(key);

        const char *ext_str = yyjson_get_str(key);
        const char *lang_str = yyjson_get_str(val);

        if (!ext_str || !lang_str) {
            pmm_log_warn("userconfig.skip_non_string", "file", source_label);
            continue;
        }

        /* Extension must start with '.' */
        if (ext_str[0] != '.') {
            pmm_log_warn("userconfig.skip_bad_ext", "file", source_label, "ext", ext_str);
            continue;
        }

        CBMLanguage lang = lang_from_string(lang_str);
        if (lang == PMM_LANG_COUNT) {
            pmm_log_warn("userconfig.unknown_lang", "file", source_label, "lang", lang_str);
            continue; /* fail-open: skip unknown languages */
        }

        /* Grow the array */
        pmm_userext_t *tmp = realloc(*entries, (size_t)(*count + SKIP_ONE) * sizeof(pmm_userext_t));
        if (!tmp) {
            return PMM_NOT_FOUND;
        }
        *entries = tmp;

        char *ext_copy = strdup(ext_str);
        if (!ext_copy) {
            return PMM_NOT_FOUND;
        }

        (*entries)[*count].ext = ext_copy;
        (*entries)[*count].lang = lang;
        (*count)++;
    }
    return 0;
}

/*
 * Read a JSON file and parse extra_extensions from it.
 * Silently ignores missing files. Logs warnings for corrupt JSON.
 * Returns 0 on success (or absent file), -1 on alloc failure.
 */
static int load_config_file(const char *path, pmm_userext_t **entries, int *count) {
    FILE *f = pmm_fopen(path, "rb");
    if (!f) {
        return 0; /* file absent — silently ignore */
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        return 0;
    }
    long len = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return 0;
    }

    if (len <= 0 || len > MAX_CONFIG_SIZE) {
        (void)fclose(f);
        if (len > MAX_CONFIG_SIZE) {
            pmm_log_warn("userconfig.file_too_large", "path", path);
        }
        return 0;
    }

    char *buf = malloc((size_t)len + SKIP_ONE);
    if (!buf) {
        (void)fclose(f);
        return PMM_NOT_FOUND;
    }

    size_t nread = fread(buf, SKIP_ONE, (size_t)len, f);
    (void)fclose(f);
    if (nread > (size_t)len) {
        nread = (size_t)len;
    }
    buf[nread] = '\0';

    yyjson_doc *doc = yyjson_read(buf, nread, 0);
    free(buf);

    if (!doc) {
        pmm_log_warn("userconfig.corrupt_json", "path", path);
        return 0; /* corrupt JSON — silently ignore (fail-open) */
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    int rc = parse_extra_extensions(root, entries, count, path);
    yyjson_doc_free(doc);
    return rc;
}

/* ── Public API ──────────────────────────────────────────────────── */

pmm_userconfig_t *pmm_userconfig_load(const char *repo_path) {
    pmm_userconfig_t *cfg = calloc(PMM_ALLOC_ONE, sizeof(pmm_userconfig_t));
    if (!cfg) {
        return NULL;
    }

    pmm_userext_t *entries = NULL;
    int count = 0;

    /* ── Step 1: Load global config ── */
    enum { PATH_BUF_SZ = 1280 };
    const char *cfg_base = pmm_app_config_dir();
    const char *cfg_fallback = cfg_base ? cfg_base : "/tmp";
    char global_path[PATH_BUF_SZ];
    snprintf(global_path, sizeof(global_path), "%s/project-mind-mcp/config.json", cfg_fallback);

    if (load_config_file(global_path, &entries, &count) != 0) {
        for (int i = 0; i < count; i++) {
            free(entries[i].ext);
        }
        free(entries);
        free(cfg);
        return NULL;
    }

    int global_count = count; /* entries[0..global_count) are from global */

    /* ── Step 2: Load project config ── */
    if (repo_path && repo_path[0]) {
        char project_path[PATH_BUF_SZ];
        snprintf(project_path, sizeof(project_path), "%s/.project-mind-mcp.json", repo_path);

        if (load_config_file(project_path, &entries, &count) != 0) {
            /* Free already-allocated entries */
            for (int i = 0; i < count; i++) {
                free(entries[i].ext);
            }
            free(entries);
            free(cfg);
            return NULL;
        }
    }

    /*
     * ── Step 3: Dedup — project entries win over global ──
     *
     * For any extension that appears in both global (indices 0..global_count)
     * and project (indices global_count..count), remove the global entry by
     * replacing it with the last global entry (order-insensitive dedup).
     */
    for (int p = global_count; p < count; p++) {
        for (int g = 0; g < global_count; g++) {
            if (entries[g].ext && strcmp(entries[g].ext, entries[p].ext) == 0) {
                /* Remove global entry: overwrite with last global entry */
                free(entries[g].ext);
                entries[g] = entries[global_count - SKIP_ONE];
                entries[global_count - SKIP_ONE].ext = NULL; /* mark as consumed */
                global_count--;
                break;
            }
        }
    }

    /*
     * Compact: remove any NULL-ext slots left by the dedup step.
     * (Those are the consumed "last global" entries.)
     */
    int write_idx = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].ext != NULL) {
            entries[write_idx++] = entries[i];
        }
    }
    count = write_idx;

    cfg->entries = entries;
    cfg->count = count;
    return cfg;
}

CBMLanguage pmm_userconfig_lookup(const pmm_userconfig_t *cfg, const char *ext) {
    if (!cfg || !ext || !ext[0]) {
        return PMM_LANG_COUNT;
    }
    for (int i = 0; i < cfg->count; i++) {
        if (cfg->entries[i].ext && strcmp(cfg->entries[i].ext, ext) == 0) {
            return cfg->entries[i].lang;
        }
    }
    return PMM_LANG_COUNT;
}

void pmm_userconfig_free(pmm_userconfig_t *cfg) {
    if (!cfg) {
        return;
    }
    for (int i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].ext);
    }
    free(cfg->entries);
    free(cfg);
}
