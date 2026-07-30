/*
 * test_log.c — RED phase tests for foundation/log.
 *
 * Testing logging is tricky since output goes to stderr.
 * We redirect stderr to a pipe and read back the output.
 */
#include "test_framework.h"
#include "../src/foundation/log.h"
#include "../src/foundation/compat.h"
#include <stdbool.h>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <io.h>
#include <fcntl.h>
#endif

/* Simple strstr wrapper used by log tests (avoids circular dep on str_util) */
static inline bool pmm_str_contains_raw(const char *s, const char *sub) {
    return strstr(s, sub) != NULL;
}

static char log_buf[4096];
static char sink_buf[4096];
static int saved_stderr;
static int pipe_fds[2];

static void test_log_sink(const char *line) {
    snprintf(sink_buf, sizeof(sink_buf), "%s", line ? line : "");
}

static void capture_start(void) {
    fflush(stderr);
    saved_stderr = dup(STDERR_FILENO);
    pmm_pipe(pipe_fds);
#ifndef _WIN32
    /* Set read end to non-blocking */
    fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
#endif
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);
}

static const char *capture_end(void) {
    fflush(stderr);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    ssize_t n = read(pipe_fds[0], log_buf, sizeof(log_buf) - 1);
    close(pipe_fds[0]);
    if (n < 0)
        n = 0;
    log_buf[n] = '\0';
    return log_buf;
}

TEST(log_level_default) {
    /* Default level should be INFO */
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_INFO);
    PASS();
}

TEST(log_level_set) {
    pmm_log_set_level(PMM_LOG_WARN);
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_WARN);
    pmm_log_set_level(PMM_LOG_INFO); /* restore */
    PASS();
}

TEST(log_info_output) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    capture_start();
    pmm_log_info("test.msg", "key1", "val1", "key2", "val2");
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "level=info"));
    ASSERT(pmm_str_contains_raw(output, "msg=test.msg"));
    ASSERT(pmm_str_contains_raw(output, "key1=val1"));
    ASSERT(pmm_str_contains_raw(output, "key2=val2"));
    PASS();
}

TEST(log_filtered_by_level) {
    pmm_log_set_level(PMM_LOG_WARN);
    capture_start();
    pmm_log_info("should.not.appear");
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    /* Should be empty — info is below warn threshold */
    ASSERT_EQ(strlen(output), 0);
    PASS();
}

TEST(log_error_output) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    capture_start();
    pmm_log_error("critical.fail", "err", "OOM");
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "level=error"));
    ASSERT(pmm_str_contains_raw(output, "msg=critical.fail"));
    ASSERT(pmm_str_contains_raw(output, "err=OOM"));
    PASS();
}

TEST(log_int_helper) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    capture_start();
    pmm_log_int(PMM_LOG_INFO, "pass.timing", "elapsed_ms", 42);
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "elapsed_ms=42"));
    PASS();
}

TEST(log_json_output) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    pmm_log_set_format(PMM_LOG_FORMAT_JSON);
    capture_start();
    pmm_log_info("test.msg", "key1", "val1", "key2", "line\nbreak");
    const char *output = capture_end();
    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "\"level\":\"info\""));
    ASSERT(pmm_str_contains_raw(output, "\"event\":\"test.msg\""));
    ASSERT(pmm_str_contains_raw(output, "\"key1\":\"val1\""));
    ASSERT(pmm_str_contains_raw(output, "\"key2\":\"line\\nbreak\""));
    PASS();
}

TEST(log_text_sanitizes_control_chars) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    capture_start();
    pmm_log_info("test\nmsg", "key", "line\r\nbreak\tvalue");
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "msg=test_msg"));
    ASSERT(pmm_str_contains_raw(output, "key=line__break_value"));
    ASSERT_EQ(output[strlen(output) - 1], '\n');
    ASSERT_NULL(strchr(output, '\r'));
    PASS();
}

TEST(log_sink_tee_keeps_stderr) {
    sink_buf[0] = '\0';
    pmm_log_set_level(PMM_LOG_DEBUG);
    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    pmm_log_set_sink_ex(test_log_sink, PMM_LOG_SINK_TEE);
    capture_start();
    pmm_log_info("tee.msg", "key", "val");
    const char *output = capture_end();
    pmm_log_set_sink(NULL);
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "msg=tee.msg"));
    ASSERT(pmm_str_contains_raw(sink_buf, "msg=tee.msg"));
    PASS();
}

TEST(log_operational_helpers) {
    pmm_log_set_level(PMM_LOG_DEBUG);
    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    capture_start();
    pmm_log_mcp_request("tools/call", "search_graph", false, 1250);
    pmm_log_http_request("graph_ui", "GET", "/api/layout", 200, 7, 0, 42);
    const char *output = capture_end();
    pmm_log_set_level(PMM_LOG_INFO);

    ASSERT(pmm_str_contains_raw(output, "msg=mcp.request"));
    ASSERT(pmm_str_contains_raw(output, "protocol=jsonrpc"));
    ASSERT(pmm_str_contains_raw(output, "method=tools/call"));
    ASSERT(pmm_str_contains_raw(output, "tool=search_graph"));
    ASSERT(pmm_str_contains_raw(output, "msg=http.request"));
    ASSERT(pmm_str_contains_raw(output, "method=GET"));
    ASSERT(pmm_str_contains_raw(output, "path=/api/layout"));
    ASSERT(pmm_str_contains_raw(output, "status=200"));
    PASS();
}

TEST(log_format_from_env) {
    pmm_setenv("PMM_LOG_FORMAT", "json", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_format(), PMM_LOG_FORMAT_JSON);

    pmm_setenv("PMM_LOG_FORMAT", "text", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_format(), PMM_LOG_FORMAT_TEXT);

    pmm_unsetenv("PMM_LOG_FORMAT");
    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    PASS();
}

TEST(log_format_unset_keeps_current) {
    pmm_unsetenv("PMM_LOG_FORMAT");
    pmm_log_set_format(PMM_LOG_FORMAT_JSON);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_format(), PMM_LOG_FORMAT_JSON);

    pmm_log_set_format(PMM_LOG_FORMAT_TEXT);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_format(), PMM_LOG_FORMAT_TEXT);

    PASS();
}

/* PMM_LOG_LEVEL parsing — distilled from #414 (closes #413). */
TEST(log_level_from_env_textual) {
    pmm_setenv("PMM_LOG_LEVEL", "error", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_ERROR);

    pmm_setenv("PMM_LOG_LEVEL", "debug", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_DEBUG);

    pmm_setenv("PMM_LOG_LEVEL", "none", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_NONE);

    /* Matching is case-insensitive */
    pmm_setenv("PMM_LOG_LEVEL", "WARN", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_WARN);

    pmm_setenv("PMM_LOG_LEVEL", "Info", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_INFO);

    pmm_unsetenv("PMM_LOG_LEVEL");
    pmm_log_set_level(PMM_LOG_INFO); /* restore */
    PASS();
}

TEST(log_level_from_env_numeric) {
    /* 0=debug 1=info 2=warn 3=error 4=none — mirrors CBMLogLevel */
    pmm_setenv("PMM_LOG_LEVEL", "0", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_DEBUG);

    pmm_setenv("PMM_LOG_LEVEL", "3", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_ERROR);

    pmm_setenv("PMM_LOG_LEVEL", "4", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_NONE);

    /* Out-of-range numeric is ignored — level unchanged */
    pmm_log_set_level(PMM_LOG_INFO);
    pmm_setenv("PMM_LOG_LEVEL", "5", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_INFO);

    pmm_unsetenv("PMM_LOG_LEVEL");
    pmm_log_set_level(PMM_LOG_INFO); /* restore */
    PASS();
}

TEST(log_level_from_env_invalid_ignored) {
    /* Unknown string and empty/unset both leave the level unchanged (fail-open) */
    pmm_log_set_level(PMM_LOG_WARN);
    pmm_setenv("PMM_LOG_LEVEL", "verbose", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_WARN);

    pmm_setenv("PMM_LOG_LEVEL", "", 1);
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_WARN);

    pmm_unsetenv("PMM_LOG_LEVEL");
    pmm_log_init_from_env();
    ASSERT_EQ(pmm_log_get_level(), PMM_LOG_WARN);

    pmm_log_set_level(PMM_LOG_INFO); /* restore */
    PASS();
}

SUITE(log) {
    RUN_TEST(log_level_default);
    RUN_TEST(log_level_set);
    RUN_TEST(log_info_output);
    RUN_TEST(log_filtered_by_level);
    RUN_TEST(log_error_output);
    RUN_TEST(log_int_helper);
    RUN_TEST(log_json_output);
    RUN_TEST(log_text_sanitizes_control_chars);
    RUN_TEST(log_sink_tee_keeps_stderr);
    RUN_TEST(log_operational_helpers);
    RUN_TEST(log_format_from_env);
    RUN_TEST(log_format_unset_keeps_current);
    RUN_TEST(log_level_from_env_textual);
    RUN_TEST(log_level_from_env_numeric);
    RUN_TEST(log_level_from_env_invalid_ignored);
}
