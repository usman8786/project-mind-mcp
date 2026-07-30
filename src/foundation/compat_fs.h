/*
 * compat_fs.h — Portable directory iteration, popen, and file operations.
 *
 * POSIX: thin wrappers around opendir/readdir, popen/pclose, mkdir, unlink.
 * Windows: FindFirstFile/FindNextFile, _popen/_pclose, _mkdir, _unlink.
 */
#ifndef PMM_COMPAT_FS_H
#define PMM_COMPAT_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ── Directory iteration ──────────────────────────────────────── */

/* Max filename length (MAX_PATH on Windows, NAME_MAX on POSIX). */
#define PMM_DIRENT_NAME_MAX 260

typedef struct pmm_dir pmm_dir_t;

typedef struct {
    char name[PMM_DIRENT_NAME_MAX];
    bool is_dir;
    unsigned char d_type; /* DT_REG, DT_DIR, DT_LNK, etc. (POSIX only, 0 on Windows) */
} pmm_dirent_t;

/* Open a directory for iteration. Returns NULL on error. */
pmm_dir_t *pmm_opendir(const char *path);

/* Read next entry. Returns NULL when done. The returned pointer is
 * valid until the next pmm_readdir call on the same handle. */
pmm_dirent_t *pmm_readdir(pmm_dir_t *d);

/* Close directory handle. */
void pmm_closedir(pmm_dir_t *d);

/* ── Portable popen/pclose ────────────────────────────────────── */

FILE *pmm_popen(const char *cmd, const char *mode);
int pmm_pclose(FILE *f);

/* ── File operations ──────────────────────────────────────────── */

/* Create directory (and parents). mode is ignored on Windows. Returns true on success. */
bool pmm_mkdir_p(const char *path, int mode);

/* Delete a file. Returns 0 on success. */
int pmm_unlink(const char *path);
/* Remove <db_path>-wal/-shm/-journal. MUST be called by any path installing a fresh
 * DB file where a previous generation lived — a leftover WAL is otherwise
 * replayed on top of the new file at the next open (#897). Returns 0 when
 * every artifact is absent, -1 when cleanup could not be safely completed. */
int pmm_remove_db_sidecars(const char *db_path);
/* rename() that replaces an existing destination on every platform
 * (Windows rename fails with EEXIST; this uses write-through MoveFileExW). */
int pmm_rename_replace(const char *src, const char *dst);
/* Canonicalize an EXISTING path and resolve links/junctions (realpath / wide
 * GetFinalPathNameByHandleW). Locale-independent on Windows — never routes
 * UTF-8 through the ANSI CRT (#973). out must be >= 4096 bytes. Returns 1 on
 * success, 0 otherwise. */
int pmm_canonical_path(const char *path, char *out, size_t out_sz);

/* Delete an empty directory. Returns 0 on success. */
int pmm_rmdir(const char *path);

/* Open a file by UTF-8 path.
 * On Windows, converts to wide-char and calls _wfopen so paths with
 * non-ASCII characters (accents, CJK, etc.) are handled correctly.
 * On POSIX, delegates to fopen. mode must be an ASCII string. */
FILE *pmm_fopen(const char *path, const char *mode);

/* Execute a command without shell interpretation.
 * argv is a NULL-terminated array: {"cmd", "arg1", "arg2", NULL}.
 * Returns the process exit code, or -1 on fork/exec failure.
 * POSIX: fork() + execvp(). Windows: CreateProcess with proper quoting. */
int pmm_exec_no_shell(const char *const *argv);

#endif /* PMM_COMPAT_FS_H */
