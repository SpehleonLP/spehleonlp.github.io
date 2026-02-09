#include "debug_output.h"
#include <string.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
static char g_debug_prefix[512] = "/";
#else
static char g_debug_prefix[512] = "./";
#endif

void set_debug_output_prefix(const char* prefix) {
    if (!prefix) {
#ifdef __EMSCRIPTEN__
        strcpy(g_debug_prefix, "/");
#else
        strcpy(g_debug_prefix, "./");
#endif
        return;
    }
    strncpy(g_debug_prefix, prefix, sizeof(g_debug_prefix) - 1);
    g_debug_prefix[sizeof(g_debug_prefix) - 1] = '\0';
}

const char* debug_path(const char* suffix, char* buf, size_t bufsize) {
    if (!suffix || !buf || bufsize == 0) return NULL;

    size_t total = strlen(g_debug_prefix) + strlen(suffix) + 1;
    if (total > bufsize) return NULL;

    snprintf(buf, bufsize, "%s%s", g_debug_prefix, suffix);
    return buf;
}
