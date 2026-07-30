/*
 * progress_sink.h — Human-readable progress for one-shot CLI commands.
 *
 * Installs a log sink that maps structured pipeline events to phase labels.
 * Interactive terminals enable it automatically; --progress forces it when
 * stderr is redirected.
 * Usage:
 *   pmm_progress_sink_init(stderr);
 *   // ... run pipeline ...
 *   pmm_progress_sink_fini();
 */
#ifndef PMM_PROGRESS_SINK_H
#define PMM_PROGRESS_SINK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Interactive terminals get lifecycle feedback automatically. --progress
 * forces the same behavior for redirected stderr without touching stdout. */
bool pmm_cli_progress_enabled(bool explicitly_requested, bool stderr_is_tty);
void pmm_cli_progress_start(FILE *out, const char *tool_name);
void pmm_cli_progress_finish(FILE *out, const char *tool_name, bool success, uint64_t elapsed_ms);

void pmm_progress_sink_init(FILE *out);
void pmm_progress_sink_fini(void);
void pmm_progress_sink_fn(const char *line);

#endif
