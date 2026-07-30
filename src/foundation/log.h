/*
 * log.h — Structured key-value logging to stderr.
 *
 * Design:
 *   - All output goes to stderr (stdout is reserved for MCP JSON-RPC)
 *   - Structured text format: "level=info msg=pass.timing pass=defs elapsed_ms=42"
 *   - Optional JSON format for local structured parsing
 *   - Levels: DEBUG, INFO, WARN, ERROR
 *   - Level filtering at runtime via pmm_log_set_level() or the
 *     PMM_LOG_LEVEL env var (see pmm_log_init_from_env)
 *   - Thread-safe (each fprintf is atomic on POSIX for lines < PIPE_BUF)
 */
#ifndef PMM_LOG_H
#define PMM_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PMM_LOG_DEBUG = 0,
    PMM_LOG_INFO = 1,
    PMM_LOG_WARN = 2,
    PMM_LOG_ERROR = 3,
    PMM_LOG_NONE = 4 /* disable all logging */
} CBMLogLevel;

typedef enum {
    PMM_LOG_FORMAT_TEXT = 0,
    PMM_LOG_FORMAT_JSON = 1,
} CBMLogFormat;

typedef enum {
    PMM_LOG_SINK_REPLACE = 0,
    PMM_LOG_SINK_TEE = 1,
} CBMLogSinkMode;

/* Apply the PMM_LOG_LEVEL environment variable to the runtime log level.
 * Accepts (case-insensitive) "debug", "info", "warn", "error", "none", or
 * the numeric equivalents 0..4 matching CBMLogLevel. Unknown, empty, or
 * unset values leave the level unchanged (fail-open).
 *
 * Also applies PMM_LOG_FORMAT=text|json. If unset, the current format is left
 * unchanged. Call once at startup before any threads or log lines. */
void pmm_log_init_from_env(void);

/* Set minimum log level (default: INFO). */
void pmm_log_set_level(CBMLogLevel level);

/* Get current log level. */
CBMLogLevel pmm_log_get_level(void);

/* Set/get output format. Default is text. */
void pmm_log_set_format(CBMLogFormat format);
CBMLogFormat pmm_log_get_format(void);

/* Core logging function. msg is a short semantic tag.
 * Variadic args are key-value pairs: (const char *key, const char *value)...
 * Terminated by NULL key.
 *
 * Example:
 *   pmm_log(PMM_LOG_INFO, "pass.timing",
 *           "pass", "defs", "elapsed_ms", "42", NULL);
 *
 * Output:
 *   level=info msg=pass.timing pass=defs elapsed_ms=42
 */
void pmm_log(CBMLogLevel level, const char *msg, ...);

/* Convenience macros. */
#define pmm_log_debug(msg, ...) pmm_log(PMM_LOG_DEBUG, msg, ##__VA_ARGS__, NULL)
#define pmm_log_info(msg, ...) pmm_log(PMM_LOG_INFO, msg, ##__VA_ARGS__, NULL)

/* Always-delivered internal control/discovery record. It bypasses the level
 * threshold and always uses the JSON encoding, so exact values (paths with
 * spaces or control bytes) survive unambiguously; it flows through the
 * configured sink like every other record. Reserve it for the rare
 * discovery/control events that ordinary log filtering must never suppress
 * (e.g. diagnostics.start path announcement). */
void pmm_log_control_record(const char *msg, ...);
#define pmm_log_control(msg, ...) pmm_log_control_record(msg, ##__VA_ARGS__, NULL)
#define pmm_log_warn(msg, ...) pmm_log(PMM_LOG_WARN, msg, ##__VA_ARGS__, NULL)
#define pmm_log_error(msg, ...) pmm_log(PMM_LOG_ERROR, msg, ##__VA_ARGS__, NULL)

/* Log with integer value (avoids sprintf for common case). */
void pmm_log_int(CBMLogLevel level, const char *msg, const char *key, int64_t value);

/* Operational event helpers. They deliberately avoid request bodies, headers,
 * arguments, and query strings. */
void pmm_log_mcp_request(const char *method, const char *tool_name, bool is_error,
                         int64_t duration_us);
void pmm_log_http_request(const char *component, const char *method, const char *path, int status,
                          int64_t duration_ms, size_t request_bytes, size_t response_bytes);

/* Optional log sink callback — called with the formatted log line. */
typedef void (*pmm_log_sink_fn)(const char *line);
void pmm_log_set_sink(pmm_log_sink_fn fn);
void pmm_log_set_sink_ex(pmm_log_sink_fn fn, CBMLogSinkMode mode);

#endif /* PMM_LOG_H */
