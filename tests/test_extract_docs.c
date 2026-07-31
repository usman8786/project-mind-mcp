/*
 * test_extract_docs.c — Doc IR parsing for Tier-A formats + DOCX/PDF goldens.
 */
#include "test_framework.h"
#include "extract_docs.h"
#include <string.h>
#include <stdlib.h>

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

TEST(docs_parse_md) {
    const char *md = "# Intro\nHello\n\n## Auth\nUse JWT\n";
    pmm_doc_ir_t ir;
    ASSERT_EQ(pmm_doc_parse_text("docs/a.md", PMM_DOC_KIND_MD, md, strlen(md), &ir), 0);
    ASSERT_TRUE(ir.section_count >= 2);
    ASSERT_TRUE(strstr(ir.sections[0].heading, "Intro") != NULL ||
                strstr(ir.sections[1].heading, "Auth") != NULL);
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_parse_txt) {
    const char *txt = "Para one.\n\nPara two about rollback.\n";
    pmm_doc_ir_t ir;
    ASSERT_EQ(pmm_doc_parse_text("docs/a.txt", PMM_DOC_KIND_TXT, txt, strlen(txt), &ir), 0);
    ASSERT_TRUE(ir.section_count >= 1);
    ASSERT_TRUE(has_body_substr(&ir, "rollback"));
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_parse_rst) {
    const char *rst = "Title\n=====\n\nBody\n\nNext\n----\n\nMore\n";
    pmm_doc_ir_t ir;
    ASSERT_EQ(pmm_doc_parse_text("docs/a.rst", PMM_DOC_KIND_RST, rst, strlen(rst), &ir), 0);
    ASSERT_TRUE(ir.section_count >= 1);
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_parse_yaml) {
    const char *yaml = "name: demo\nport: 4000\n";
    pmm_doc_ir_t ir;
    ASSERT_EQ(pmm_doc_parse_text("docs/x.yaml", PMM_DOC_KIND_YAML, yaml, strlen(yaml), &ir), 0);
    ASSERT_TRUE(ir.section_count >= 1);
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_parse_json) {
    const char *json = "{\"routes\":{\"/health\":{\"get\":true}}}\n";
    pmm_doc_ir_t ir;
    ASSERT_EQ(pmm_doc_parse_text("docs/x.json", PMM_DOC_KIND_JSON, json, strlen(json), &ir), 0);
    ASSERT_TRUE(ir.section_count >= 1);
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_path_kinds) {
    ASSERT_EQ(pmm_doc_path_is_document("a.pdf"), 1);
    ASSERT_EQ(pmm_doc_path_is_document("a.docx"), 1);
    ASSERT_EQ(pmm_doc_path_is_document("a.c"), 0);
    PASS();
}

TEST(docs_fixture_md) {
    pmm_doc_ir_t ir;
    memset(&ir, 0, sizeof(ir));
    ASSERT_EQ(pmm_doc_parse_file("tests/fixtures/docs/sample.md", "docs/sample.md", &ir), 0);
    ASSERT_EQ(ir.kind, PMM_DOC_KIND_MD);
    ASSERT_TRUE(ir.section_count >= 1);
    ASSERT_TRUE(has_body_substr(&ir, "JWT") || has_body_substr(&ir, "Auth"));
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_fixture_docx) {
    pmm_doc_ir_t ir;
    memset(&ir, 0, sizeof(ir));
    ASSERT_EQ(pmm_doc_parse_file("tests/fixtures/docs/sample.docx", "docs/sample.docx", &ir), 0);
    ASSERT_EQ(ir.kind, PMM_DOC_KIND_DOCX);
    ASSERT_TRUE(ir.section_count >= 1);
    ASSERT_TRUE(has_body_substr(&ir, "Docx") || has_body_substr(&ir, "authenticate"));
    pmm_doc_ir_free(&ir);
    PASS();
}

TEST(docs_fixture_pdf) {
    pmm_doc_ir_t ir;
    memset(&ir, 0, sizeof(ir));
    ASSERT_EQ(pmm_doc_parse_file("tests/fixtures/docs/sample.pdf", "docs/sample.pdf", &ir), 0);
    ASSERT_EQ(ir.kind, PMM_DOC_KIND_PDF);
    ASSERT_TRUE(ir.section_count >= 1);
    ASSERT_TRUE(has_body_substr(&ir, "PdfGoldenText"));
    pmm_doc_ir_free(&ir);
    PASS();
}

SUITE(extract_docs) {
    RUN_TEST(docs_parse_md);
    RUN_TEST(docs_parse_txt);
    RUN_TEST(docs_parse_rst);
    RUN_TEST(docs_parse_yaml);
    RUN_TEST(docs_parse_json);
    RUN_TEST(docs_path_kinds);
    RUN_TEST(docs_fixture_md);
    RUN_TEST(docs_fixture_docx);
    RUN_TEST(docs_fixture_pdf);
}
