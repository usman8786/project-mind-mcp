/*
 * test_extract_docs.c — Doc IR parsing for Tier-A formats + DOCX/PDF goldens.
 */
#include "extract_docs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
}

static int has_body_substr(const pmm_doc_ir_t *ir, const char *needle) {
    for (int i = 0; i < ir->section_count; i++) {
        if (ir->sections[i].body && strstr(ir->sections[i].body, needle)) {
            return 1;
        }
        if (ir->sections[i].heading && strstr(ir->sections[i].heading, needle)) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    {
        const char *md = "# Intro\nHello\n\n## Auth\nUse JWT\n";
        pmm_doc_ir_t ir;
        expect(pmm_doc_parse_text("docs/a.md", PMM_DOC_KIND_MD, md, strlen(md), &ir) == 0,
               "parse md");
        expect(ir.section_count >= 2, "md sections");
        expect(strstr(ir.sections[0].heading, "Intro") != NULL ||
                   strstr(ir.sections[1].heading, "Auth") != NULL,
               "md headings");
        pmm_doc_ir_free(&ir);
    }
    {
        const char *txt = "Para one.\n\nPara two about rollback.\n";
        pmm_doc_ir_t ir;
        expect(pmm_doc_parse_text("docs/a.txt", PMM_DOC_KIND_TXT, txt, strlen(txt), &ir) == 0,
               "parse txt");
        expect(ir.section_count >= 1, "txt sections");
        expect(has_body_substr(&ir, "rollback"), "txt body");
        pmm_doc_ir_free(&ir);
    }
    {
        const char *rst = "Title\n=====\n\nBody\n\nNext\n----\n\nMore\n";
        pmm_doc_ir_t ir;
        expect(pmm_doc_parse_text("docs/a.rst", PMM_DOC_KIND_RST, rst, strlen(rst), &ir) == 0,
               "parse rst");
        expect(ir.section_count >= 1, "rst sections");
        pmm_doc_ir_free(&ir);
    }
    {
        const char *yaml = "name: demo\nport: 4000\n";
        pmm_doc_ir_t ir;
        expect(pmm_doc_parse_text("docs/x.yaml", PMM_DOC_KIND_YAML, yaml, strlen(yaml), &ir) == 0,
               "parse yaml");
        expect(ir.section_count >= 1, "yaml sections");
        pmm_doc_ir_free(&ir);
    }
    {
        const char *json = "{\"routes\":{\"/health\":{\"get\":true}}}\n";
        pmm_doc_ir_t ir;
        expect(pmm_doc_parse_text("docs/x.json", PMM_DOC_KIND_JSON, json, strlen(json), &ir) == 0,
               "parse json");
        expect(ir.section_count >= 1, "json sections");
        pmm_doc_ir_free(&ir);
    }
    {
        expect(pmm_doc_path_is_document("a.pdf") == 1, "pdf is doc");
        expect(pmm_doc_path_is_document("a.docx") == 1, "docx is doc");
        expect(pmm_doc_path_is_document("a.c") == 0, "c not doc");
    }
    {
        pmm_doc_ir_t ir;
        memset(&ir, 0, sizeof(ir));
        if (pmm_doc_parse_file("tests/fixtures/docs/sample.md", "docs/sample.md", &ir) == 0) {
            expect(ir.kind == PMM_DOC_KIND_MD, "fixture md kind");
            expect(ir.section_count >= 1, "fixture md sections");
            expect(has_body_substr(&ir, "JWT") || has_body_substr(&ir, "Auth"), "fixture md auth");
            pmm_doc_ir_free(&ir);
        } else {
            expect(0, "fixture md parse");
        }
    }
    {
        pmm_doc_ir_t ir;
        memset(&ir, 0, sizeof(ir));
        if (pmm_doc_parse_file("tests/fixtures/docs/sample.docx", "docs/sample.docx", &ir) == 0) {
            expect(ir.kind == PMM_DOC_KIND_DOCX, "fixture docx kind");
            expect(ir.section_count >= 1, "fixture docx sections");
            expect(has_body_substr(&ir, "Docx") || has_body_substr(&ir, "authenticate"),
                   "fixture docx text");
            pmm_doc_ir_free(&ir);
        } else {
            expect(0, "fixture docx parse");
        }
    }
    {
        pmm_doc_ir_t ir;
        memset(&ir, 0, sizeof(ir));
        if (pmm_doc_parse_file("tests/fixtures/docs/sample.pdf", "docs/sample.pdf", &ir) == 0) {
            expect(ir.kind == PMM_DOC_KIND_PDF, "fixture pdf kind");
            expect(ir.section_count >= 1, "fixture pdf sections");
            expect(has_body_substr(&ir, "PdfGoldenText"), "fixture pdf text");
            pmm_doc_ir_free(&ir);
        } else {
            expect(0, "fixture pdf parse");
        }
    }
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("test_extract_docs: ok\n");
    return 0;
}
