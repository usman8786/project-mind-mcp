/*
 * extract_docs.h — Document IR and format adapters for project context docs.
 * All formats normalize into the same sectioned IR used by pass_docs.
 */
#ifndef PMM_EXTRACT_DOCS_H
#define PMM_EXTRACT_DOCS_H

#include <stddef.h>

typedef enum {
    PMM_DOC_KIND_UNKNOWN = 0,
    PMM_DOC_KIND_MD,
    PMM_DOC_KIND_TXT,
    PMM_DOC_KIND_RST,
    PMM_DOC_KIND_YAML,
    PMM_DOC_KIND_JSON,
    PMM_DOC_KIND_DOCX,
    PMM_DOC_KIND_PDF
} pmm_doc_kind_t;

typedef struct {
    char *anchor;
    char *heading;
    char *body;
    int start_line;
    int end_line;
} pmm_doc_section_t;

typedef struct {
    char *path;
    char *title;
    pmm_doc_kind_t kind;
    char *content_hash; /* sha256 hex or empty */
    pmm_doc_section_t *sections;
    int section_count;
} pmm_doc_ir_t;

pmm_doc_kind_t pmm_doc_kind_from_path(const char *path);
const char *pmm_doc_kind_name(pmm_doc_kind_t kind);

/* True when path should enter the docs pass (by extension). */
int pmm_doc_path_is_document(const char *path);

/* Parse UTF-8 text (already loaded) into Doc IR. path/title are copied. */
int pmm_doc_parse_text(const char *path, pmm_doc_kind_t kind, const char *text, size_t text_len,
                       pmm_doc_ir_t *out);

/* Load file from disk and parse (text formats + docx/pdf adapters). */
int pmm_doc_parse_file(const char *abs_or_rel_path, const char *display_path, pmm_doc_ir_t *out);

void pmm_doc_ir_free(pmm_doc_ir_t *doc);

#endif /* PMM_EXTRACT_DOCS_H */
