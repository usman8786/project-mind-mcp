/*
 * extract_docs.c — Tier-A text + DOCX/PDF adapters → Doc IR.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "extract_docs.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { DOC_MAX_SECTIONS = 512, DOC_MAX_BODY = 128 * 1024, DOC_CHUNK = 3500 };

static char *doc_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

static char *doc_strndup(const char *s, size_t n) {
    char *o = malloc(n + 1);
    if (!o) {
        return NULL;
    }
    memcpy(o, s, n);
    o[n] = '\0';
    return o;
}

pmm_doc_kind_t pmm_doc_kind_from_path(const char *path) {
    if (!path) {
        return PMM_DOC_KIND_UNKNOWN;
    }
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return PMM_DOC_KIND_UNKNOWN;
    }
    if (strcmp(dot, ".md") == 0 || strcmp(dot, ".mdx") == 0) {
        return PMM_DOC_KIND_MD;
    }
    if (strcmp(dot, ".txt") == 0 || strcmp(dot, ".text") == 0) {
        return PMM_DOC_KIND_TXT;
    }
    if (strcmp(dot, ".rst") == 0) {
        return PMM_DOC_KIND_RST;
    }
    if (strcmp(dot, ".yaml") == 0 || strcmp(dot, ".yml") == 0) {
        return PMM_DOC_KIND_YAML;
    }
    if (strcmp(dot, ".json") == 0) {
        return PMM_DOC_KIND_JSON;
    }
    if (strcmp(dot, ".docx") == 0) {
        return PMM_DOC_KIND_DOCX;
    }
    if (strcmp(dot, ".pdf") == 0) {
        return PMM_DOC_KIND_PDF;
    }
    return PMM_DOC_KIND_UNKNOWN;
}

const char *pmm_doc_kind_name(pmm_doc_kind_t kind) {
    switch (kind) {
    case PMM_DOC_KIND_MD:
        return "md";
    case PMM_DOC_KIND_TXT:
        return "txt";
    case PMM_DOC_KIND_RST:
        return "rst";
    case PMM_DOC_KIND_YAML:
        return "yaml";
    case PMM_DOC_KIND_JSON:
        return "json";
    case PMM_DOC_KIND_DOCX:
        return "docx";
    case PMM_DOC_KIND_PDF:
        return "pdf";
    default:
        return "unknown";
    }
}

int pmm_doc_path_is_document(const char *path) {
    return pmm_doc_kind_from_path(path) != PMM_DOC_KIND_UNKNOWN;
}

void pmm_doc_ir_free(pmm_doc_ir_t *doc) {
    if (!doc) {
        return;
    }
    free(doc->path);
    free(doc->title);
    free(doc->content_hash);
    for (int i = 0; i < doc->section_count; i++) {
        free(doc->sections[i].anchor);
        free(doc->sections[i].heading);
        free(doc->sections[i].body);
    }
    free(doc->sections);
    memset(doc, 0, sizeof(*doc));
}

static int doc_add_section(pmm_doc_ir_t *doc, const char *anchor, const char *heading,
                           const char *body, int start_line, int end_line) {
    if (!doc || doc->section_count >= DOC_MAX_SECTIONS) {
        return -1;
    }
    pmm_doc_section_t *grown =
        realloc(doc->sections, (size_t)(doc->section_count + 1) * sizeof(*grown));
    if (!grown) {
        return -1;
    }
    doc->sections = grown;
    pmm_doc_section_t *s = &doc->sections[doc->section_count];
    memset(s, 0, sizeof(*s));
    s->anchor = doc_strdup(anchor ? anchor : "");
    s->heading = doc_strdup(heading ? heading : "");
    s->body = doc_strdup(body ? body : "");
    s->start_line = start_line;
    s->end_line = end_line;
    if (!s->anchor || !s->heading || !s->body) {
        free(s->anchor);
        free(s->heading);
        free(s->body);
        return -1;
    }
    doc->section_count++;
    return 0;
}

static void slugify(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
        } else if (c == ' ' || c == '-' || c == '_' || c == '/') {
            if (j > 0 && out[j - 1] != '-') {
                out[j++] = '-';
            }
        }
    }
    while (j > 0 && out[j - 1] == '-') {
        j--;
    }
    out[j] = '\0';
    if (j == 0) {
        snprintf(out, out_sz, "section");
    }
}

static int parse_markdown_or_rst(pmm_doc_ir_t *doc, const char *text, int is_rst) {
    const char *p = text;
    int line = 1;
    char heading[512] = "Introduction";
    char anchor[256];
    slugify(heading, anchor, sizeof(anchor));
    size_t body_cap = 8192;
    size_t body_len = 0;
    char *body = malloc(body_cap);
    if (!body) {
        return -1;
    }
    body[0] = '\0';
    int start_line = 1;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        int is_heading = 0;
        char next_heading[512];

        if (!is_rst && llen > 0 && p[0] == '#') {
            size_t i = 0;
            while (i < llen && p[i] == '#') {
                i++;
            }
            while (i < llen && (p[i] == ' ' || p[i] == '\t')) {
                i++;
            }
            size_t hlen = llen - i;
            if (hlen >= sizeof(next_heading)) {
                hlen = sizeof(next_heading) - 1;
            }
            memcpy(next_heading, p + i, hlen);
            next_heading[hlen] = '\0';
            is_heading = 1;
        } else if (is_rst && eol) {
            const char *next = eol + 1;
            const char *neol = strchr(next, '\n');
            size_t nlen = neol ? (size_t)(neol - next) : strlen(next);
            if (llen > 0 && nlen == llen && nlen > 0) {
                char ch = next[0];
                int underline = (ch == '=' || ch == '-' || ch == '~' || ch == '^' || ch == '"');
                for (size_t k = 1; underline && k < nlen; k++) {
                    if (next[k] != ch) {
                        underline = 0;
                    }
                }
                if (underline) {
                    size_t hlen = llen < sizeof(next_heading) - 1 ? llen : sizeof(next_heading) - 1;
                    memcpy(next_heading, p, hlen);
                    next_heading[hlen] = '\0';
                    is_heading = 1;
                    /* skip underline line in outer loop by advancing after push */
                    p = neol ? neol + 1 : next + nlen;
                    line += 2;
                    goto flush_heading;
                }
            }
        }

        if (is_heading) {
        flush_heading:
            if (body_len > 0 || doc->section_count == 0) {
                doc_add_section(doc, anchor, heading, body, start_line, line - 1);
            }
            snprintf(heading, sizeof(heading), "%s", next_heading);
            slugify(heading, anchor, sizeof(anchor));
            body_len = 0;
            body[0] = '\0';
            start_line = line;
            if (!is_rst) {
                p = eol ? eol + 1 : p + llen;
                line++;
            }
            continue;
        }

        if (body_len + llen + 2 > body_cap) {
            size_t nc = body_cap * 2;
            if (nc > DOC_MAX_BODY) {
                nc = DOC_MAX_BODY;
            }
            if (body_len + llen + 2 > nc) {
                /* flush chunk */
                doc_add_section(doc, anchor, heading, body, start_line, line);
                body_len = 0;
                body[0] = '\0';
                start_line = line;
                char cont[256];
                snprintf(cont, sizeof(cont), "%.200s-cont-%d", anchor, doc->section_count);
                memcpy(anchor, cont, sizeof(anchor));
                anchor[sizeof(anchor) - 1] = '\0';
            } else {
                char *nb = realloc(body, nc);
                if (!nb) {
                    free(body);
                    return -1;
                }
                body = nb;
                body_cap = nc;
            }
        }
        if (llen > 0 && body_len + llen + 2 <= body_cap) {
            memcpy(body + body_len, p, llen);
            body_len += llen;
            body[body_len++] = '\n';
            body[body_len] = '\0';
        }
        p = eol ? eol + 1 : p + llen;
        line++;
    }
    if (body_len > 0 || doc->section_count == 0) {
        doc_add_section(doc, anchor, heading, body, start_line, line);
    }
    free(body);
    return 0;
}

static int parse_plain_chunks(pmm_doc_ir_t *doc, const char *text) {
    size_t len = strlen(text);
    int part = 0;
    size_t off = 0;
    int line = 1;
    while (off < len && doc->section_count < DOC_MAX_SECTIONS) {
        size_t take = len - off;
        if (take > DOC_CHUNK) {
            take = DOC_CHUNK;
            /* break on newline if possible */
            for (size_t i = take; i > take / 2; i--) {
                if (text[off + i] == '\n') {
                    take = i + 1;
                    break;
                }
            }
        }
        char anchor[64];
        char heading[64];
        snprintf(anchor, sizeof(anchor), "chunk-%d", part);
        snprintf(heading, sizeof(heading), "Part %d", part + 1);
        char *body = doc_strndup(text + off, take);
        if (!body) {
            return -1;
        }
        int lines = 1;
        for (size_t i = 0; i < take; i++) {
            if (text[off + i] == '\n') {
                lines++;
            }
        }
        doc_add_section(doc, anchor, heading, body, line, line + lines - 1);
        free(body);
        line += lines;
        off += take;
        part++;
    }
    if (doc->section_count == 0) {
        doc_add_section(doc, "body", "Document", text, 1, 1);
    }
    return 0;
}

/* Shallow YAML/JSON: emit one section per non-empty line groups under key-ish lines. */
static int parse_structured(pmm_doc_ir_t *doc, const char *text) {
    const char *p = text;
    int line = 1;
    char current_key[256] = "root";
    char anchor[256];
    slugify(current_key, anchor, sizeof(anchor));
    size_t body_cap = 4096;
    size_t body_len = 0;
    char *body = malloc(body_cap);
    int start_line = 1;
    if (!body) {
        return -1;
    }
    body[0] = '\0';

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        /* detect "key:" at start (yaml) or "key": (json) */
        int key_line = 0;
        char keybuf[256];
        size_t ki = 0;
        while (ki < llen && (p[ki] == ' ' || p[ki] == '\t')) {
            ki++;
        }
        size_t ks = ki;
        if (ki < llen && (p[ki] == '"' || p[ki] == '\'')) {
            char q = p[ki++];
            while (ki < llen && p[ki] != q) {
                ki++;
            }
            if (ki < llen) {
                ki++;
            }
        } else {
            while (ki < llen && (isalnum((unsigned char)p[ki]) || p[ki] == '_' || p[ki] == '-' ||
                                 p[ki] == '/' || p[ki] == '.')) {
                ki++;
            }
        }
        size_t ke = ki;
        while (ki < llen && (p[ki] == ' ' || p[ki] == '\t')) {
            ki++;
        }
        if (ke > ks && ki < llen && p[ki] == ':') {
            size_t kn = ke - ks;
            if (kn >= sizeof(keybuf)) {
                kn = sizeof(keybuf) - 1;
            }
            memcpy(keybuf, p + ks, kn);
            keybuf[kn] = '\0';
            if (keybuf[0] == '"' || keybuf[0] == '\'') {
                /* strip quotes */
                size_t L = strlen(keybuf);
                if (L >= 2) {
                    memmove(keybuf, keybuf + 1, L - 2);
                    keybuf[L - 2] = '\0';
                }
            }
            key_line = 1;
        }

        if (key_line && body_len > 0) {
            doc_add_section(doc, anchor, current_key, body, start_line, line - 1);
            body_len = 0;
            body[0] = '\0';
            snprintf(current_key, sizeof(current_key), "%s", keybuf);
            slugify(current_key, anchor, sizeof(anchor));
            start_line = line;
        } else if (key_line && body_len == 0) {
            snprintf(current_key, sizeof(current_key), "%s", keybuf);
            slugify(current_key, anchor, sizeof(anchor));
            start_line = line;
        }

        if (body_len + llen + 2 > body_cap && body_cap < DOC_MAX_BODY) {
            size_t nc = body_cap * 2;
            char *nb = realloc(body, nc);
            if (nb) {
                body = nb;
                body_cap = nc;
            }
        }
        if (llen > 0 && body_len + llen + 2 <= body_cap) {
            memcpy(body + body_len, p, llen);
            body_len += llen;
            body[body_len++] = '\n';
            body[body_len] = '\0';
        }
        p = eol ? eol + 1 : p + llen;
        line++;
    }
    if (body_len > 0 || doc->section_count == 0) {
        doc_add_section(doc, anchor, current_key, body, start_line, line);
    }
    free(body);
    return 0;
}

int pmm_doc_parse_text(const char *path, pmm_doc_kind_t kind, const char *text, size_t text_len,
                       pmm_doc_ir_t *out) {
    if (!path || !text || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->path = doc_strdup(path);
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *base2 = strrchr(path, '\\');
    if (!base || (base2 && base2 > base)) {
        base = base2;
    }
#endif
    out->title = doc_strdup(base ? base + 1 : path);
    out->kind = kind;
    /* FNV-1a 64-bit content identity */
    {
        uint64_t h = 14695981039346656037ULL;
        for (size_t i = 0; i < text_len; i++) {
            h ^= (unsigned char)text[i];
            h *= 1099511628211ULL;
        }
        char hex[32];
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
        out->content_hash = doc_strdup(hex);
    }
    if (!out->path || !out->title || !out->content_hash) {
        pmm_doc_ir_free(out);
        return -1;
    }

    int rc = 0;
    switch (kind) {
    case PMM_DOC_KIND_MD:
        rc = parse_markdown_or_rst(out, text, 0);
        break;
    case PMM_DOC_KIND_RST:
        rc = parse_markdown_or_rst(out, text, 1);
        break;
    case PMM_DOC_KIND_YAML:
    case PMM_DOC_KIND_JSON:
        rc = parse_structured(out, text);
        break;
    case PMM_DOC_KIND_TXT:
    default:
        rc = parse_plain_chunks(out, text);
        break;
    }
    if (rc != 0) {
        pmm_doc_ir_free(out);
    }
    return rc;
}

/* ── Minimal ZIP (stored + deflate) for DOCX ───────────────────── */

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static unsigned char *inflate_raw(const unsigned char *src, uint32_t src_len, uint32_t dst_len) {
    unsigned char *dst = malloc(dst_len + 1);
    if (!dst) {
        return NULL;
    }
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        free(dst);
        return NULL;
    }
    zs.next_in = (Bytef *)(uintptr_t)src;
    zs.avail_in = src_len;
    zs.next_out = dst;
    zs.avail_out = dst_len;
    int zrc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zrc != Z_STREAM_END && zs.total_out == 0) {
        free(dst);
        return NULL;
    }
    if (zs.total_out < dst_len) {
        dst_len = (uint32_t)zs.total_out;
    }
    dst[dst_len] = '\0';
    return dst;
}

static unsigned char *zip_find_file(const unsigned char *data, size_t len, const char *want,
                                    size_t *out_len) {
    if (len < 22) {
        return NULL;
    }
    /* scan local file headers */
    size_t pos = 0;
    while (pos + 30 < len) {
        if (rd32(data + pos) != 0x04034b50u) {
            break;
        }
        uint16_t method = rd16(data + pos + 8);
        uint32_t comp = rd32(data + pos + 18);
        uint32_t uncomp = rd32(data + pos + 22);
        uint16_t nlen = rd16(data + pos + 26);
        uint16_t elen = rd16(data + pos + 28);
        size_t name_off = pos + 30;
        size_t data_off = name_off + nlen + elen;
        if (data_off + comp > len) {
            return NULL;
        }
        if (nlen == strlen(want) && memcmp(data + name_off, want, nlen) == 0) {
            *out_len = uncomp;
            if (method == 0) {
                unsigned char *out = malloc(uncomp + 1);
                if (!out) {
                    return NULL;
                }
                memcpy(out, data + data_off, uncomp);
                out[uncomp] = '\0';
                return out;
            }
            if (method == 8) {
                return inflate_raw(data + data_off, comp, uncomp);
            }
            return NULL;
        }
        pos = data_off + comp;
    }
    return NULL;
}

static char *strip_xml_to_text(const char *xml) {
    size_t cap = strlen(xml) + 1;
    char *out = malloc(cap);
    if (!out) {
        return NULL;
    }
    size_t j = 0;
    int in_tag = 0;
    for (size_t i = 0; xml[i]; i++) {
        char c = xml[i];
        if (c == '<') {
            in_tag = 1;
            /* paragraph breaks */
            if (strncmp(xml + i, "</w:p>", 6) == 0 || strncmp(xml + i, "<w:br", 5) == 0) {
                if (j + 1 < cap) {
                    out[j++] = '\n';
                }
            }
            continue;
        }
        if (c == '>') {
            in_tag = 0;
            continue;
        }
        if (!in_tag) {
            if (j + 1 < cap) {
                out[j++] = c;
            }
        }
    }
    out[j] = '\0';
    return out;
}

static int parse_docx_bytes(const char *path, const unsigned char *data, size_t len,
                            pmm_doc_ir_t *out) {
    size_t xml_len = 0;
    unsigned char *xml = zip_find_file(data, len, "word/document.xml", &xml_len);
    if (!xml) {
        return -1;
    }
    char *text = strip_xml_to_text((const char *)xml);
    free(xml);
    if (!text) {
        return -1;
    }
    int rc = pmm_doc_parse_text(path, PMM_DOC_KIND_DOCX, text, strlen(text), out);
    free(text);
    return rc;
}

/* Best-effort PDF: extract printable Latin strings from content streams. */
static int parse_pdf_bytes(const char *path, const unsigned char *data, size_t len,
                           pmm_doc_ir_t *out) {
    size_t cap = len / 2 + 1024;
    if (cap > DOC_MAX_BODY) {
        cap = DOC_MAX_BODY;
    }
    char *text = malloc(cap);
    if (!text) {
        return -1;
    }
    size_t j = 0;
    for (size_t i = 0; i + 1 < len && j + 2 < cap; i++) {
        /* (string) Tj or similar — collect balanced parens strings */
        if (data[i] == '(') {
            i++;
            while (i < len && data[i] != ')' && j + 2 < cap) {
                unsigned char c = data[i++];
                if (c == '\\' && i < len) {
                    c = data[i++];
                }
                if (c >= 32 && c < 127) {
                    text[j++] = (char)c;
                } else if (c == '\n' || c == '\r') {
                    text[j++] = '\n';
                }
            }
            if (j + 1 < cap) {
                text[j++] = ' ';
            }
        }
    }
    text[j] = '\0';
    if (j < 8) {
        free(text);
        return -1;
    }
    int rc = pmm_doc_parse_text(path, PMM_DOC_KIND_PDF, text, j, out);
    free(text);
    return rc;
}

static int read_file_bytes(const char *path, unsigned char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 32 * 1024 * 1024) {
        fclose(f);
        return -1;
    }
    rewind(f);
    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 0;
}

int pmm_doc_parse_file(const char *abs_or_rel_path, const char *display_path, pmm_doc_ir_t *out) {
    if (!abs_or_rel_path || !out) {
        return -1;
    }
    const char *disp = display_path ? display_path : abs_or_rel_path;
    pmm_doc_kind_t kind = pmm_doc_kind_from_path(disp);
    if (kind == PMM_DOC_KIND_UNKNOWN) {
        kind = pmm_doc_kind_from_path(abs_or_rel_path);
    }
    unsigned char *buf = NULL;
    size_t len = 0;
    if (read_file_bytes(abs_or_rel_path, &buf, &len) != 0) {
        return -1;
    }
    int rc = -1;
    if (kind == PMM_DOC_KIND_DOCX) {
        rc = parse_docx_bytes(disp, buf, len, out);
    } else if (kind == PMM_DOC_KIND_PDF) {
        rc = parse_pdf_bytes(disp, buf, len, out);
    } else {
        if (kind == PMM_DOC_KIND_UNKNOWN) {
            kind = PMM_DOC_KIND_TXT;
        }
        rc = pmm_doc_parse_text(disp, kind, (const char *)buf, len, out);
    }
    free(buf);
    return rc;
}
