/* Symbols config-file.c needs from platform.c / platform-nix.c /
 * platform-windows.c, copied from those files so the oracle does not pull
 * them in wholesale. os_fopen and the mbs/wcs stubs live in
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

/* platform.c: locale independent double conversion, from jansson. */
static inline void to_locale(char *str)
{
	const char *point;
	char *pos;

	point = localeconv()->decimal_point;
	if (*point == '.') {
		/* No conversion needed */
		return;
	}

	pos = strchr(str, '.');
	if (pos)
		*pos = *point;
}

static inline void from_locale(char *buffer)
{
	const char *point;
	char *pos;

	point = localeconv()->decimal_point;
	if (*point == '.') {
		/* No conversion needed */
		return;
	}

	pos = strchr(buffer, *point);
	if (pos)
		*pos = '.';
}

/* Test-only exports called from Rust over FFI. */
double os_strtod(const char *str);
int os_dtostr(double value, char *dst, size_t size);

/* platform.c */
double os_strtod(const char *str)
{
	char buf[64];
	strncpy(buf, str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	to_locale(buf);
	return strtod(buf, NULL);
}

/* platform.c */
int os_dtostr(double value, char *dst, size_t size)
{
	int ret;
	char *start, *end;
	size_t length;

	ret = snprintf(dst, size, "%.17g", value);
	if (ret < 0)
		return -1;

	length = (size_t)ret;
	if (length >= size)
		return -1;

	from_locale(dst);

	/* Make sure there's a dot or 'e' in the output. Otherwise
	   a real is converted to an integer when decoding */
	if (strchr(dst, '.') == NULL && strchr(dst, 'e') == NULL) {
		if (length + 3 >= size) {
			/* No space to append ".0" */
			return -1;
		}
		dst[length] = '.';
		dst[length + 1] = '0';
		dst[length + 2] = '\0';
		length += 2;
	}

	/* Remove leading '+' from positive exponent. Also remove leading
	   zeros from exponents (added by some printf() implementations) */
	start = strchr(dst, 'e');
	if (start) {
		start++;
		end = start + 1;

		if (*start == '-')
			start++;

		while (*end == '0')
			end++;

		if (end != start) {
			memmove(start, end, length - (size_t)(end - dst));
			length -= (size_t)(end - start);
		}
	}

	return (int)length;
}

/* os_file_exists is the Rust platform shim on Unix and Windows. */
