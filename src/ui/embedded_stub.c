/*
 * embedded_stub.c — Empty asset table when built without frontend.
 *
 * Used by the standard `pmm` target (no Node.js required).
 * The `pmm-with-ui` target replaces this with generated embedded_assets.c.
 */
#include "ui/embedded_assets.h"

#include <stddef.h>
#include <string.h>

pmm_embedded_file_t PMM_EMBEDDED_FILES[] = {{NULL, NULL, 0, NULL}};
const int PMM_EMBEDDED_FILE_COUNT = 0;

const pmm_embedded_file_t *pmm_embedded_lookup(const char *path) {
    for (int i = 0; i < PMM_EMBEDDED_FILE_COUNT; i++) {
        if (strcmp(PMM_EMBEDDED_FILES[i].path, path) == 0) {
            return &PMM_EMBEDDED_FILES[i];
        }
    }
    return NULL;
}
