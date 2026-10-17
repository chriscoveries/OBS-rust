/* Test-only oracle: the original libobs/util/cf-tokenizer.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the
 * Rust implementation, plus the struct layouts as the C compiler sees them.
 * The lexer.c functions it calls are renamed too, so it runs on the lexer
 * oracle (oracle/lexer.c) rather than on the Rust lexer port. */
#include <stddef.h>

#define cf_literal_to_str oracle_cf_literal_to_str
#define cf_lexer_init oracle_cf_lexer_init
#define cf_lexer_free oracle_cf_lexer_free
#define cf_lexer_lex oracle_cf_lexer_lex

#define strref_cmp oracle_strref_cmp
#define strref_cmpi oracle_strref_cmpi
#define strref_cmp_strref oracle_strref_cmp_strref
#define strref_cmpi_strref oracle_strref_cmpi_strref
#define valid_int_str oracle_valid_int_str
#define valid_float_str oracle_valid_float_str
#define error_data_add oracle_error_data_add
#define error_data_buildstring oracle_error_data_buildstring
#define lexer_getbasetoken oracle_lexer_getbasetoken
#define lexer_getstroffset oracle_lexer_getstroffset

#include "util/cf-tokenizer.c.h"

/* The declarations are part of the macros: the exports have no header. */
#define LAYOUT(type)                          \
	size_t oracle_##type##_size(void);    \
	size_t oracle_##type##_align(void);   \
	size_t oracle_##type##_size(void)     \
	{                                     \
		return sizeof(struct type);   \
	}                                     \
	size_t oracle_##type##_align(void)    \
	{                                     \
		return _Alignof(struct type); \
	}
#define OFFSET(type, field)                          \
	size_t oracle_##type##_offset_##field(void); \
	size_t oracle_##type##_offset_##field(void)  \
	{                                            \
		return offsetof(struct type, field); \
	}

LAYOUT(cf_token)
OFFSET(cf_token, lex)
OFFSET(cf_token, str)
OFFSET(cf_token, unmerged_str)
OFFSET(cf_token, type)

LAYOUT(cf_lexer)
OFFSET(cf_lexer, file)
OFFSET(cf_lexer, base_lexer)
OFFSET(cf_lexer, reformatted)
OFFSET(cf_lexer, write_offset)
OFFSET(cf_lexer, tokens)
OFFSET(cf_lexer, unexpected_eof)

/* Test-only export called from Rust over FFI. */
size_t oracle_cf_token_type_size(void);

size_t oracle_cf_token_type_size(void)
{
	return sizeof(enum cf_token_type);
}
