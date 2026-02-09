#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H

#include <stddef.h>

/*
 * Configurable debug output prefix.
 *
 * On Emscripten the default is "/".
 * For native CLI builds, call set_debug_output_prefix() to set the
 * path prefix for all debug PNGs (e.g. "./output/lic_").
 *
 * debug_path("fadeIn.png") -> "{prefix}fadeIn.png"
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Set the path prefix for debug output files.
 * The string is copied internally. Pass NULL to reset to default. */
void set_debug_output_prefix(const char* prefix);

/* Build a full path by prepending the configured prefix to a suffix.
 * Example: debug_path("fadeIn.png", buf, sizeof(buf))
 *   -> "/fadeIn.png"              (Emscripten default)
 *   -> "./output/lic_fadeIn.png"  (after set_debug_output_prefix("./output/lic_"))
 *
 * Returns buf, or NULL if buf is too small. */
const char* debug_path(const char* suffix, char* buf, size_t bufsize);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DEBUG_OUTPUT_H */
