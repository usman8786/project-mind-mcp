/*
 * compat_regex.h — Portable regular expression API.
 *
 * POSIX: direct wrappers around <regex.h> (regcomp, regexec, regfree).
 * Windows: TODO — vendor TRE regex or use a C++ wrapper around <regex>.
 *
 * Uses our own types so callers never include <regex.h> directly.
 */
#ifndef PMM_COMPAT_REGEX_H
#define PMM_COMPAT_REGEX_H

#include "foundation/constants.h"
#include <stddef.h>

/* ── Flags ────────────────────────────────────────────────────── */

#define PMM_REG_EXTENDED 1
#define PMM_REG_ICASE 2
#define PMM_REG_NOSUB 4
#define PMM_REG_NEWLINE 8

/* ── Error codes ──────────────────────────────────────────────── */

#define PMM_REG_OK 0
#define PMM_REG_NOMATCH (-1)

/* ── Types ────────────────────────────────────────────────────── */

/* Opaque regex handle — sized to hold the platform's regex_t. */
typedef struct {
    /* PMM_SZ_256 bytes should be large enough for any platform's regex_t.
     * POSIX regex_t is typically 48-PMM_SZ_64 bytes; TRE is ~80 bytes. */
    char opaque[PMM_SZ_256];
} pmm_regex_t;

typedef struct {
    int rm_so; /* byte offset of match start, -1 if no match */
    int rm_eo; /* byte offset past match end */
} pmm_regmatch_t;

/* ── Functions ────────────────────────────────────────────────── */

/* Compile a regular expression. Returns PMM_REG_OK on success, non-zero on error. */
int pmm_regcomp(pmm_regex_t *r, const char *pattern, int flags);

/* Execute compiled regex against str. nmatch/matches may be 0/NULL.
 * eflags: 0 or combination of platform-specific exec flags.
 * Returns PMM_REG_OK on match, PMM_REG_NOMATCH on no match. */
int pmm_regexec(const pmm_regex_t *r, const char *str, int nmatch, pmm_regmatch_t *matches,
                int eflags);

/* Free compiled regex. */
void pmm_regfree(pmm_regex_t *r);

#endif /* PMM_COMPAT_REGEX_H */
