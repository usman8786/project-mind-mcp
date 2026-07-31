/*
 * pass_docs.c — Upsert Document / DocSection nodes from in-repo + attached docs.
 */
#include "pipeline/pipeline_internal.h"
#include "extract_docs.h"
#include "store/store.h"
#include "foundation/log.h"
#include "foundation/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_escape_into(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 2 < out_sz; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if ((unsigned char)c < 32) {
            /* skip */
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static void upsert_doc_ir(pmm_pipeline_ctx_t *ctx, const pmm_doc_ir_t *doc, const char *source) {
    if (!ctx || !doc || !doc->path) {
        return;
    }
    char doc_qn[1024];
    snprintf(doc_qn, sizeof(doc_qn), "%s.__doc__.%s", ctx->project_name, doc->path);

    char esc_path[512], esc_kind[64], esc_hash[80], esc_src[32];
    json_escape_into(doc->path, esc_path, sizeof(esc_path));
    json_escape_into(pmm_doc_kind_name(doc->kind), esc_kind, sizeof(esc_kind));
    json_escape_into(doc->content_hash ? doc->content_hash : "", esc_hash, sizeof(esc_hash));
    json_escape_into(source ? source : "in_repo", esc_src, sizeof(esc_src));

    char props[1024];
    snprintf(props, sizeof(props),
             "{\"kind\":\"%s\",\"hash\":\"%s\",\"source\":\"%s\",\"path\":\"%s\"}", esc_kind,
             esc_hash, esc_src, esc_path);

    const char *title = doc->title ? doc->title : doc->path;
    int64_t doc_id =
        pmm_gbuf_upsert_node(ctx->gbuf, "Document", title, doc_qn, doc->path, 0, 0, props);

    const pmm_gbuf_node_t *project = pmm_gbuf_find_by_qn(ctx->gbuf, ctx->project_name);
    if (project && doc_id > 0) {
        pmm_gbuf_insert_edge(ctx->gbuf, project->id, doc_id, "CONTAINS", "{}");
    }

    for (int i = 0; i < doc->section_count; i++) {
        const pmm_doc_section_t *sec = &doc->sections[i];
        char sec_qn[1280];
        snprintf(sec_qn, sizeof(sec_qn), "%s#%s", doc_qn, sec->anchor ? sec->anchor : "s");

        char esc_h[256], esc_a[256], esc_b[768];
        json_escape_into(sec->heading ? sec->heading : "", esc_h, sizeof(esc_h));
        json_escape_into(sec->anchor ? sec->anchor : "", esc_a, sizeof(esc_a));
        /* truncate body in props; full body lives in name+properties body field */
        const char *body = sec->body ? sec->body : "";
        char body_trunc[700];
        size_t bl = strlen(body);
        if (bl > sizeof(body_trunc) - 1) {
            bl = sizeof(body_trunc) - 1;
        }
        memcpy(body_trunc, body, bl);
        body_trunc[bl] = '\0';
        json_escape_into(body_trunc, esc_b, sizeof(esc_b));

        char sprops[1600];
        snprintf(sprops, sizeof(sprops),
                 "{\"anchor\":\"%s\",\"heading\":\"%s\",\"body\":\"%s\",\"doc_path\":\"%s\"}",
                 esc_a, esc_h, esc_b, esc_path);

        const char *sname = sec->heading && sec->heading[0] ? sec->heading : sec->anchor;
        int64_t sid =
            pmm_gbuf_upsert_node(ctx->gbuf, "DocSection", sname ? sname : "section", sec_qn,
                                 doc->path, sec->start_line, sec->end_line, sprops);
        if (doc_id > 0 && sid > 0) {
            pmm_gbuf_insert_edge(ctx->gbuf, doc_id, sid, "HAS_SECTION", "{}");
        }

        /* Best-effort MENTIONS: match section heading/body tokens to Function names */
        if (sid > 0 && body[0]) {
            const pmm_gbuf_node_t **funcs = NULL;
            int fcount = 0;
            if (pmm_gbuf_find_by_label(ctx->gbuf, "Function", &funcs, &fcount) == 0) {
                int linked = 0;
                for (int fi = 0; fi < fcount && linked < 8; fi++) {
                    const char *fname = funcs[fi]->name;
                    if (!fname || strlen(fname) < 4) {
                        continue;
                    }
                    if (strstr(body, fname) || (sec->heading && strstr(sec->heading, fname))) {
                        pmm_gbuf_insert_edge(ctx->gbuf, sid, funcs[fi]->id, "MENTIONS", "{}");
                        linked++;
                    }
                }
            }
        }
    }
}

static int path_under_docs_dir(const char *rel) {
    return rel && (strncmp(rel, "docs/", 5) == 0 || strncmp(rel, "doc/", 4) == 0 ||
                   strstr(rel, "/docs/") != NULL);
}

static int is_readme(const char *rel) {
    if (!rel) {
        return 0;
    }
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    return strncmp(base, "README", 6) == 0;
}

static void process_one_path(pmm_pipeline_ctx_t *ctx, const char *abs_path, const char *display,
                             const char *source) {
    pmm_doc_ir_t ir;
    memset(&ir, 0, sizeof(ir));
    if (pmm_doc_parse_file(abs_path, display, &ir) != 0) {
        return;
    }
    upsert_doc_ir(ctx, &ir, source);
    pmm_doc_ir_free(&ir);
}

int pmm_pipeline_pass_docs(pmm_pipeline_ctx_t *ctx, const pmm_file_info_t *files, int file_count) {
    if (!ctx || !ctx->gbuf || !ctx->repo_path) {
        return 0;
    }
    pmm_log_info("pass.start", "pass", "docs", "files", "0");

    /* In-repo documents from discover list */
    for (int i = 0; i < file_count; i++) {
        const char *rel = files[i].rel_path;
        if (!pmm_doc_path_is_document(rel)) {
            continue;
        }
        /* Prefer docs/, README*, and all md/rst/txt; also yaml/json under docs/ */
        pmm_doc_kind_t k = pmm_doc_kind_from_path(rel);
        int include = path_under_docs_dir(rel) || is_readme(rel) || k == PMM_DOC_KIND_MD ||
                      k == PMM_DOC_KIND_RST || k == PMM_DOC_KIND_TXT || k == PMM_DOC_KIND_DOCX ||
                      k == PMM_DOC_KIND_PDF;
        if (!include && (k == PMM_DOC_KIND_YAML || k == PMM_DOC_KIND_JSON)) {
            include = path_under_docs_dir(rel);
        }
        if (!include) {
            continue;
        }
        char abs[2048];
        snprintf(abs, sizeof(abs), "%s/%s", ctx->repo_path, rel);
        process_one_path(ctx, abs, rel, "in_repo");
    }

    /* Attached documents from store registry */
    char db_path[1024];
    const char *cache = pmm_resolve_cache_dir();
    if (cache && ctx->project_name) {
        snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, ctx->project_name);
        pmm_store_t *store = pmm_store_open_path_query(db_path);
        if (store) {
            pmm_project_doc_t *docs = NULL;
            int dcount = 0;
            if (pmm_store_docs_list(store, ctx->project_name, &docs, &dcount) == PMM_STORE_OK) {
                for (int i = 0; i < dcount; i++) {
                    if (!docs[i].enabled || !docs[i].abs_path) {
                        continue;
                    }
                    process_one_path(ctx, docs[i].abs_path, docs[i].abs_path, "attached");
                }
                pmm_store_docs_free(docs, dcount);
            }
            pmm_store_close(store);
        }
    }

    return 0;
}
