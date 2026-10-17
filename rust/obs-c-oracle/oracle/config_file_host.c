/* Symbols config-file.c needs from platform.c / platform-nix.c /
 * platform-windows.c, copied from those files so the oracle does not pull
 * them in wholesale. os_strtod/os_dtostr are the real platform-number.c,
 * from oracle/platform_number.c. os_fopen and the mbs/wcs stubs live in
 * file_serializer_host.c. Unix os_file_exists is the Rust platform-nix
 * shim. */
#define _FILE_OFFSET_BITS 64
#ifndef _WIN32
/* fseeko/ftello are POSIX; -std=c11 hides them without this. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <wchar.h>

void *bmalloc(size_t size);
void bfree(void *ptr);
int64_t os_ftelli64(FILE *file);
int astrcmp_n(const char *str1, const char *str2, size_t n);
int astrcmpi(const char *str1, const char *str2);

/* os_fread_utf8 comes from oracle/test_stubs.c (faithful to platform.c,
 * which returns 0). */

/* os_file_exists is the Rust platform shim on Unix and Windows. */
