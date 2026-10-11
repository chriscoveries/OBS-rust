#include "output-filename-format.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static char replace_reserved(char c)
{
	switch (c) {
	case '\\':
		return '/';
	case '*':
	case '?':
	case '"':
	case '|':
	case ':':
	case '>':
	case '<':
		return '_';
	default:
		return c;
	}
}

char *output_filename_format(const char *format, const char *prefix, const char *suffix)
{
	if (!format)
		format = "";
	if (!prefix)
		prefix = "";
	if (!suffix)
		suffix = "";

	size_t format_len = strlen(format);
	size_t prefix_len = strlen(prefix);
	size_t suffix_len = strlen(suffix);
	bool prefix_space = prefix_len && prefix[prefix_len - 1] != ' ';
	bool suffix_space = suffix_len && suffix[0] != ' ';

	/* The prefix goes in front of the last path component. */
	size_t insert_pos = 0;
	for (size_t i = 0; i < format_len; i++) {
		if (format[i] == '/' || format[i] == '\\')
			insert_pos = i + 1;
	}

	char *out = malloc(format_len + prefix_len + prefix_space + suffix_space + suffix_len + 1);
	if (!out)
		return NULL;

	char *p = out;
	memcpy(p, format, insert_pos);
	p += insert_pos;
	memcpy(p, prefix, prefix_len);
	p += prefix_len;
	if (prefix_space)
		*p++ = ' ';
	memcpy(p, format + insert_pos, format_len - insert_pos);
	p += format_len - insert_pos;
	if (suffix_space)
		*p++ = ' ';
	memcpy(p, suffix, suffix_len);
	p += suffix_len;
	*p = '\0';

	for (p = out; *p; p++)
		*p = replace_reserved(*p);

	return out;
}
