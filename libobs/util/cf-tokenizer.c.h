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

static inline void cf_convert_from_escape_literal(char **p_dst, const char **p_src)
{
	char *dst = *p_dst;
	const char *src = *p_src;

	switch (*(src++)) {
	case '\'':
		*(dst++) = '\'';
		break;
	case '\"':
		*(dst++) = '\"';
		break;
	case '\?':
		*(dst++) = '\?';
		break;
	case '\\':
		*(dst++) = '\\';
		break;
	case '0':
		*(dst++) = '\0';
		break;
	case 'a':
		*(dst++) = '\a';
		break;
	case 'b':
		*(dst++) = '\b';
		break;
	case 'f':
		*(dst++) = '\f';
		break;
	case 'n':
		*(dst++) = '\n';
		break;
	case 'r':
		*(dst++) = '\r';
		break;
	case 't':
		*(dst++) = '\t';
		break;
	case 'v':
		*(dst++) = '\v';
		break;

	/* hex */
	case 'X':
	case 'x':
		*(dst++) = (char)strtoul(src, NULL, 16);
		src += 2;
		break;

	/* oct */
	default:
		if (isdigit(*src)) {
			*(dst++) = (char)strtoul(src, NULL, 8);
			src += 3;
		}

		/* case 'u':
		case 'U': */
	}

	*p_dst = dst;
	*p_src = src;
}

char *cf_literal_to_str(const char *literal, size_t count)
{
	const char *temp_src, *end;
	char *str, *temp_dst;

	if (!count)
		count = strlen(literal);

	if (count < 2)
		return NULL;
	if (literal[0] != literal[count - 1])
		return NULL;
	if (literal[0] != '\"' && literal[0] != '\'')
		return NULL;

	/* strip leading and trailing quote characters */
	str = bzalloc(--count);
	temp_src = literal + 1;
	end = literal + count; /* the closing quote */
	temp_dst = str;

	/* An escape consumes several chars but writes at most one, so the
	 * result always fits in count bytes. */
	while (*temp_src && temp_src < end) {
		if (*temp_src == '\\') {
			temp_src++;
			cf_convert_from_escape_literal(&temp_dst, &temp_src);
		} else {
			*(temp_dst++) = *(temp_src++);
		}
	}

	*temp_dst = 0;
	return str;
}

static bool cf_is_token_break(struct base_token *start_token, const struct base_token *token)
{
	switch (start_token->type) {
	case BASETOKEN_ALPHA:
		if (token->type == BASETOKEN_OTHER || token->type == BASETOKEN_WHITESPACE)
			return true;
		break;

	case BASETOKEN_DIGIT:
		if (token->type == BASETOKEN_WHITESPACE ||
		    (token->type == BASETOKEN_OTHER && *token->text.array != '.'))
			return true;
		break;

	case BASETOKEN_WHITESPACE:
		/* lump all non-newline whitespace together when possible */
		if (is_space_or_tab(*start_token->text.array) && is_space_or_tab(*token->text.array))
			break;
		return true;

	case BASETOKEN_OTHER:
		if (*start_token->text.array == '.' && token->type == BASETOKEN_DIGIT) {
			start_token->type = BASETOKEN_DIGIT;
			break;
		}
		/* Falls through. */

	case BASETOKEN_NONE:
		return true;
	}

	return false;
}

static inline bool cf_is_splice(const char *array)
{
	return (*array == '\\' && is_newline(array[1]));
}

static inline void cf_pass_any_splices(const char **parray)
{
	while (cf_is_splice(*parray))
		*parray += 1 + newline_size((*parray) + 1);
}

static inline bool cf_is_comment(const char *array)
{
	const char *offset = array;

	if (*offset++ == '/') {
		cf_pass_any_splices(&offset);
		return (*offset == '*' || *offset == '/');
	}

	return false;
}

static bool cf_lexer_process_comment(struct cf_lexer *lex, struct cf_token *out_token)
{
	const char *offset;

	if (!cf_is_comment(out_token->unmerged_str.array))
		return false;

	offset = lex->base_lexer.offset;
	cf_pass_any_splices(&offset);

	strcpy(lex->write_offset++, " ");
	out_token->str.len = 1;

	if (*offset == '/') {
		while (*++offset && !is_newline(*offset))
			cf_pass_any_splices(&offset);

	} else if (*offset == '*') {
		bool was_star = false;
		lex->unexpected_eof = true;

		while (*++offset) {
			cf_pass_any_splices(&offset);

			if (was_star && *offset == '/') {
				offset++;
				lex->unexpected_eof = false;
				break;
			} else {
				was_star = (*offset == '*');
			}
		}
	}

	out_token->unmerged_str.len += (size_t)(offset - out_token->unmerged_str.array);
	out_token->type = CFTOKEN_SPACETAB;
	lex->base_lexer.offset = offset;

	return true;
}

static inline void cf_lexer_write_strref(struct cf_lexer *lex, const struct strref *ref)
{
	strncpy(lex->write_offset, ref->array, ref->len);
	lex->write_offset[ref->len] = 0;
	lex->write_offset += ref->len;
}

static bool cf_lexer_is_include(struct cf_lexer *lex)
{
	bool found_include_import = false;
	bool found_preprocessor = false;
	size_t i;

	for (i = lex->tokens.num; i > 0; i--) {
		struct cf_token *token = lex->tokens.array + (i - 1);

		if (is_space_or_tab(*token->str.array))
			continue;

		if (!found_include_import) {
			if (strref_cmp(&token->str, "include") != 0 && strref_cmp(&token->str, "import") != 0)
				break;

			found_include_import = true;

		} else if (!found_preprocessor) {
			if (*token->str.array != '#')
				break;

			found_preprocessor = true;

		} else {
			return is_newline(*token->str.array);
		}
	}

	/* if starting line */
	return found_preprocessor && found_include_import;
}

static void cf_lexer_getstrtoken(struct cf_lexer *lex, struct cf_token *out_token, char delimiter,
				 bool allow_escaped_delimiters)
{
	const char *offset = lex->base_lexer.offset;
	bool escaped = false;

	out_token->unmerged_str.len++;
	out_token->str.len++;
	cf_lexer_write_strref(lex, &out_token->unmerged_str);

	while (*offset) {
		cf_pass_any_splices(&offset);
		if (*offset == delimiter) {
			if (!escaped) {
				*lex->write_offset++ = *offset;
				out_token->str.len++;
				offset++;
				break;
			}
		} else if (is_newline(*offset)) {
			break;
		}

		*lex->write_offset++ = *offset;
		out_token->str.len++;

		escaped = (allow_escaped_delimiters && *offset == '\\');
		offset++;
	}

	*lex->write_offset = 0;
	out_token->unmerged_str.len += (size_t)(offset - out_token->unmerged_str.array);
	out_token->type = CFTOKEN_STRING;
	lex->base_lexer.offset = offset;
}

static bool cf_lexer_process_string(struct cf_lexer *lex, struct cf_token *out_token)
{
	char ch = *out_token->unmerged_str.array;

	if (ch == '<' && cf_lexer_is_include(lex)) {
		cf_lexer_getstrtoken(lex, out_token, '>', false);
		return true;

	} else if (ch == '"' || ch == '\'') {
		cf_lexer_getstrtoken(lex, out_token, ch, !cf_lexer_is_include(lex));
		return true;
	}

	return false;
}

static inline enum cf_token_type cf_get_token_type(const struct cf_token *token, const struct base_token *start_token)
{
	switch (start_token->type) {
	case BASETOKEN_ALPHA:
		return CFTOKEN_NAME;

	case BASETOKEN_DIGIT:
		return CFTOKEN_NUM;

	case BASETOKEN_WHITESPACE:
		if (is_newline(*token->str.array))
			return CFTOKEN_NEWLINE;
		else
			return CFTOKEN_SPACETAB;

	case BASETOKEN_NONE:
	case BASETOKEN_OTHER:
		break;
	}

	return CFTOKEN_OTHER;
}

static bool cf_lexer_nexttoken(struct cf_lexer *lex, struct cf_token *out_token)
{
	struct base_token token, start_token;
	bool wrote_data = false;

	base_token_clear(&token);
	base_token_clear(&start_token);
	cf_token_clear(out_token);

	while (lexer_getbasetoken(&lex->base_lexer, &token, PARSE_WHITESPACE)) {
		/* reclassify underscore as alpha for alnum tokens */
		if (*token.text.array == '_')
			token.type = BASETOKEN_ALPHA;

		/* ignore escaped newlines to merge spliced lines */
		if (cf_is_splice(token.text.array)) {
			lex->base_lexer.offset += newline_size(token.text.array + 1);
			continue;
		}

		if (!wrote_data) {
			out_token->unmerged_str.array = token.text.array;
			out_token->str.array = lex->write_offset;

			/* if comment then output a space */
			if (cf_lexer_process_comment(lex, out_token))
				return true;

			/* process string tokens if any */
			if (cf_lexer_process_string(lex, out_token))
				return true;

			base_token_copy(&start_token, &token);
			wrote_data = true;

		} else if (cf_is_token_break(&start_token, &token)) {
			lex->base_lexer.offset -= token.text.len;
			break;
		}

		/* write token to CF lexer to account for splicing/comments */
		cf_lexer_write_strref(lex, &token.text);
		out_token->str.len += token.text.len;
	}

	if (wrote_data) {
		out_token->unmerged_str.len = (size_t)(lex->base_lexer.offset - out_token->unmerged_str.array);
		out_token->type = cf_get_token_type(out_token, &start_token);
	}

	return wrote_data;
}

void cf_lexer_init(struct cf_lexer *lex)
{
	lexer_init(&lex->base_lexer);
	da_init(lex->tokens);

	lex->file = NULL;
	lex->reformatted = NULL;
	lex->write_offset = NULL;
	lex->unexpected_eof = false;
}

void cf_lexer_free(struct cf_lexer *lex)
{
	bfree(lex->file);
	bfree(lex->reformatted);
	lexer_free(&lex->base_lexer);
	da_free(lex->tokens);

	lex->file = NULL;
	lex->reformatted = NULL;
	lex->write_offset = NULL;
	lex->unexpected_eof = false;
}

bool cf_lexer_lex(struct cf_lexer *lex, const char *str, const char *file)
{
	struct cf_token token;
	struct cf_token *last_token = NULL;

	cf_lexer_free(lex);
	if (!str || !*str)
		return false;

	if (file)
		lex->file = bstrdup(file);

	lexer_start(&lex->base_lexer, str);
	cf_token_clear(&token);

	lex->reformatted = bmalloc(strlen(str) + 1);
	lex->reformatted[0] = 0;
	lex->write_offset = lex->reformatted;

	while (cf_lexer_nexttoken(lex, &token)) {
		if (last_token && is_space_or_tab(*last_token->str.array) && is_space_or_tab(*token.str.array)) {
			cf_token_add(last_token, &token);
			continue;
		}

		token.lex = lex;
		last_token = da_push_back_new(lex->tokens);
		memcpy(last_token, &token, sizeof(struct cf_token));
	}

	cf_token_clear(&token);

	token.str.array = lex->write_offset;
	token.unmerged_str.array = lex->base_lexer.offset;
	token.lex = lex;
	da_push_back(lex->tokens, &token);

	return !lex->unexpected_eof;
}
