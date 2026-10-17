/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "build/libobs-util.pch.h"

/* locale independent double conversion from jansson, credit goes to them */

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

double os_strtod(const char *str)
{
	char buf[64];
	strncpy(buf, str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	to_locale(buf);
	return strtod(buf, NULL);
}

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
			memmove(start, end, length - (size_t)(end - dst) + 1);
			length -= (size_t)(end - start);
		}
	}

	return (int)length;
}
