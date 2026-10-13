#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <util/bmem.h>
#include <util/cf-parser.h>

/* Characterization tests: the expected values are what libobs/util/cf-lexer.c
 * and cf-parser.c produce today, quirks included. */

static void literal_to_str_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	char *str;

	str = cf_literal_to_str("\"abc\"", 0);
	assert_non_null(str);
	assert_string_equal(str, "abc");
	bfree(str);

	/* single-quoted */
	str = cf_literal_to_str("'xy'", 0);
	assert_non_null(str);
	assert_string_equal(str, "xy");
	bfree(str);

	/* explicit count shorter than the text */
	str = cf_literal_to_str("\"abc\"zzz", 5);
	assert_non_null(str);
	assert_string_equal(str, "abc");
	bfree(str);

	/* A nonzero count bounds the readable input; no trailing NUL is needed. */
	char *counted_literal = malloc(3);
	assert_non_null(counted_literal);
	memcpy(counted_literal, "\"a\"", 3);
	str = cf_literal_to_str(counted_literal, 3);
	assert_non_null(str);
	assert_string_equal(str, "a");
	bfree(str);
	free(counted_literal);

	/*
	 * escapes: the closing quote is never part of the result. It used to
	 * leak in after an escape, because the remaining-length counter went
	 * down once per output char, not per consumed char (#48).
	 */
	str = cf_literal_to_str("\"a\\nb\"", 0);
	assert_non_null(str);
	assert_string_equal(str, "a\nb");
	bfree(str);

	str = cf_literal_to_str("\"\\t\"", 0);
	assert_non_null(str);
	assert_string_equal(str, "\t");
	bfree(str);

	/* an escaped quote right before the closing one is kept */
	str = cf_literal_to_str("\"a\\\"\"", 0);
	assert_non_null(str);
	assert_string_equal(str, "a\"");
	bfree(str);

	/* text after an explicit count is never read into the result */
	str = cf_literal_to_str("'\\x41'zz'", 6);
	assert_non_null(str);
	assert_string_equal(str, "A");
	bfree(str);

	/* invalid literals */
	assert_null(cf_literal_to_str("abc", 0));
	assert_null(cf_literal_to_str("\"abc'", 0));
	assert_null(cf_literal_to_str("\"", 0));
	assert_null(cf_literal_to_str("\"abcdef\"", 5));

	/* empty quotes */
	str = cf_literal_to_str("\"\"", 0);
	assert_non_null(str);
	assert_string_equal(str, "");
	bfree(str);

	assert_int_equal(bnum_allocs(), allocs);
}

struct expected_token {
	enum cf_token_type type;
	const char *text;
};

static void expect_tokens(const struct cf_lexer *lex, const struct expected_token *exp, size_t count)
{
	assert_int_equal(lex->tokens.num, count);

	for (size_t i = 0; i < count; i++) {
		const struct cf_token *tok = lex->tokens.array + i;

		assert_int_equal(tok->type, exp[i].type);
		assert_ptr_equal(tok->lex, lex);
		assert_int_equal(tok->str.len, strlen(exp[i].text));
		assert_int_equal(strref_cmp(&tok->str, exp[i].text), 0);
	}
}

static void lex_basic_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	cf_lexer_init(&lex);

	assert_true(cf_lexer_lex(&lex, "int x = 5; // c\n/* b */ float y;", "t.c"));
	assert_false(lex.unexpected_eof);
	assert_string_equal(lex.file, "t.c");
	/* comments are reduced to a single space, which merges with adjacent
	 * space/tab tokens */
	assert_string_equal(lex.reformatted, "int x = 5;  \n  float y;");

	const struct expected_token exp[] = {
		{CFTOKEN_NAME, "int"},
		{CFTOKEN_SPACETAB, " "},
		{CFTOKEN_NAME, "x"},
		{CFTOKEN_SPACETAB, " "},
		{CFTOKEN_OTHER, "="},
		{CFTOKEN_SPACETAB, " "},
		{CFTOKEN_NUM, "5"},
		{CFTOKEN_OTHER, ";"},
		{CFTOKEN_SPACETAB, "  "},
		{CFTOKEN_NEWLINE, "\n"},
		{CFTOKEN_SPACETAB, "  "},
		{CFTOKEN_NAME, "float"},
		{CFTOKEN_SPACETAB, " "},
		{CFTOKEN_NAME, "y"},
		{CFTOKEN_OTHER, ";"},
		/* terminator */
		{CFTOKEN_NONE, ""},
	};
	expect_tokens(&lex, exp, sizeof(exp) / sizeof(exp[0]));
	assert_ptr_equal(cf_lexer_get_tokens(&lex), lex.tokens.array);

	cf_lexer_free(&lex);
	assert_null(lex.file);
	assert_null(lex.reformatted);
	assert_int_equal(lex.tokens.num, 0);
	assert_int_equal(bnum_allocs(), allocs);
}

static void lex_string_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	cf_lexer_init(&lex);

	assert_true(cf_lexer_lex(&lex, "x = \"hi there\";", NULL));
	assert_null(lex.file);

	const struct expected_token exp[] = {
		{CFTOKEN_NAME, "x"},     {CFTOKEN_SPACETAB, " "},          {CFTOKEN_OTHER, "="},
		{CFTOKEN_SPACETAB, " "}, {CFTOKEN_STRING, "\"hi there\""}, {CFTOKEN_OTHER, ";"},
		{CFTOKEN_NONE, ""},
	};
	expect_tokens(&lex, exp, sizeof(exp) / sizeof(exp[0]));

	cf_lexer_free(&lex);
	assert_int_equal(bnum_allocs(), allocs);
}

static void lex_failure_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	cf_lexer_init(&lex);

	/* empty / NULL input fails and produces no tokens */
	assert_false(cf_lexer_lex(&lex, "", "t.c"));
	assert_int_equal(lex.tokens.num, 0);
	assert_false(cf_lexer_lex(&lex, NULL, "t.c"));
	assert_int_equal(lex.tokens.num, 0);

	/* unterminated block comment */
	assert_false(cf_lexer_lex(&lex, "a /* b", "t.c"));
	assert_true(lex.unexpected_eof);

	cf_lexer_free(&lex);
	assert_int_equal(bnum_allocs(), allocs);
}

/* concatenates the text of every token before the NONE terminator */
static void join_tokens(struct cf_token *tok, char *out, size_t out_size)
{
	size_t used = 0;

	out[0] = 0;
	for (; tok->type != CFTOKEN_NONE; tok++) {
		assert_true(used + tok->str.len < out_size);
		memcpy(out + used, tok->str.array, tok->str.len);
		used += tok->str.len;
		out[used] = 0;
	}
}

static size_t count_tokens(struct cf_token *tok)
{
	size_t n = 1; /* terminator */
	for (; tok->type != CFTOKEN_NONE; tok++)
		n++;
	return n;
}

static void preprocess_define_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	struct cf_preprocessor pp;
	struct error_data ed;
	char buf[128];

	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	assert_true(cf_lexer_lex(&lex, "#define FOO 3\nint a = FOO;", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_false(error_data_has_errors(&ed));
	assert_int_equal(ed.errors.num, 0);
	assert_int_equal(pp.defines.num, 1);

	struct cf_token *tokens = cf_preprocessor_get_tokens(&pp);
	/* directive line is dropped, FOO is replaced by " 3" (a filler space
	 * then the number) */
	assert_int_equal(count_tokens(tokens), 11);
	join_tokens(tokens, buf, sizeof(buf));
	assert_string_equal(buf, "\nint a =  3;");
	assert_int_equal(tokens[8].type, CFTOKEN_NUM);
	assert_int_equal(strref_cmp(&tokens[8].str, "3"), 0);

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);
	assert_int_equal(bnum_allocs(), allocs);
}

static void preprocess_ifdef_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	struct cf_preprocessor pp;
	struct error_data ed;
	struct cf_token *tokens;
	char buf[128];

	/* FOO undefined: the #else branch is kept */
	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	assert_true(cf_lexer_lex(&lex, "#ifdef FOO\nyes\n#else\nno\n#endif\nend", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_int_equal(ed.errors.num, 0);

	tokens = cf_preprocessor_get_tokens(&pp);
	assert_int_equal(count_tokens(tokens), 6);
	join_tokens(tokens, buf, sizeof(buf));
	assert_string_equal(buf, "\nno\n\nend");

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);

	/* FOO defined: the #ifdef branch is kept */
	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	assert_true(cf_lexer_lex(&lex, "#define FOO\n#ifdef FOO\nyes\n#else\nno\n#endif\nend", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_int_equal(ed.errors.num, 0);

	tokens = cf_preprocessor_get_tokens(&pp);
	assert_int_equal(count_tokens(tokens), 7);
	join_tokens(tokens, buf, sizeof(buf));
	assert_string_equal(buf, "\n\nyes\n\nend");

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);

	assert_int_equal(bnum_allocs(), allocs);
}

static void preprocess_errors_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	struct cf_preprocessor pp;
	struct error_data ed;
	char *msg;

	/* unterminated #ifdef */
	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	assert_true(cf_lexer_lex(&lex, "#ifdef FOO\nx", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_true(error_data_has_errors(&ed));
	assert_int_equal(ed.errors.num, 1);
	assert_int_equal(error_data_type_count(&ed, LEX_ERROR), 1);

	msg = error_data_buildstring(&ed);
	assert_string_equal(msg, "t.c (2, 2): Unexpected end of file before #endif\n");
	bfree(msg);

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);

	/* #endif with no opening block */
	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	assert_true(cf_lexer_lex(&lex, "#endif", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_true(error_data_has_errors(&ed));
	assert_int_equal(ed.errors.num, 1);

	msg = error_data_buildstring(&ed);
	assert_string_equal(msg, "t.c (1, 2): #endif outside of #if/#ifdef/#ifndef block\n");
	bfree(msg);

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);

	assert_int_equal(bnum_allocs(), allocs);
}

static void preprocess_add_remove_def_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_lexer lex;
	struct cf_preprocessor pp;
	struct error_data ed;
	struct cf_def def;
	struct cf_token tok;
	struct cf_token *tokens;
	char buf[128];

	cf_lexer_init(&lex);
	cf_preprocessor_init(&pp);
	error_data_init(&ed);

	/* hand-built definition: BAR -> 7, terminated by a NONE token */
	cf_def_init(&def);
	strref_set(&def.name.str, "BAR", 3);
	def.name.type = CFTOKEN_NAME;

	cf_token_clear(&tok);
	strref_set(&tok.str, "7", 1);
	tok.type = CFTOKEN_NUM;
	cf_def_addtoken(&def, &tok);
	cf_token_clear(&tok);
	cf_def_addtoken(&def, &tok);

	cf_preprocessor_add_def(&pp, &def);
	assert_int_equal(pp.defines.num, 1);

	assert_true(cf_lexer_lex(&lex, "x BAR y", "t.c"));
	assert_true(cf_preprocess(&pp, &lex, &ed));
	assert_int_equal(ed.errors.num, 0);

	tokens = cf_preprocessor_get_tokens(&pp);
	assert_int_equal(count_tokens(tokens), 6);
	join_tokens(tokens, buf, sizeof(buf));
	assert_string_equal(buf, "x 7 y");
	assert_int_equal(tokens[2].type, CFTOKEN_NUM);

	/* removing an unknown name is a no-op, removing BAR drops it */
	cf_preprocessor_remove_def(&pp, "NOPE");
	assert_int_equal(pp.defines.num, 1);
	cf_preprocessor_remove_def(&pp, "BAR");
	assert_int_equal(pp.defines.num, 0);

	/* with BAR gone the name is passed through untouched */
	cf_preprocessor_free(&pp);
	cf_preprocessor_init(&pp);
	assert_true(cf_preprocess(&pp, &lex, &ed));
	tokens = cf_preprocessor_get_tokens(&pp);
	assert_int_equal(count_tokens(tokens), 6);
	join_tokens(tokens, buf, sizeof(buf));
	assert_string_equal(buf, "x BAR y");

	error_data_free(&ed);
	cf_preprocessor_free(&pp);
	cf_lexer_free(&lex);
	assert_int_equal(bnum_allocs(), allocs);
}

static void parser_pass_pair_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_parser parser;
	cf_parser_init(&parser);

	assert_true(cf_parser_parse(&parser, "a { b ( c ) } d", "p.c"));
	assert_non_null(parser.cur_token);
	assert_true(cf_token_is(&parser, "a"));
	assert_int_equal(parser.cur_token->type, CFTOKEN_NAME);

	/* advance to the opening brace */
	assert_true(cf_next_token(&parser));
	assert_true(cf_token_is(&parser, "{"));
	assert_int_equal(parser.cur_token->type, CFTOKEN_OTHER);

	/* passing the pair skips everything through the closing brace */
	assert_true(cf_pass_pair(&parser, '{', '}'));
	assert_int_equal(parser.cur_token->type, CFTOKEN_SPACETAB);

	assert_true(cf_next_token(&parser));
	assert_true(cf_token_is(&parser, "d"));

	/* running off the end */
	assert_false(cf_next_token(&parser));
	assert_int_equal(parser.cur_token->type, CFTOKEN_NONE);
	assert_int_equal(error_data_has_errors(&parser.error_list), false);

	cf_parser_free(&parser);
	assert_null(parser.cur_token);

	/* nested pairs */
	cf_parser_init(&parser);
	assert_true(cf_parser_parse(&parser, "{ x { y } z } w", "p.c"));
	assert_true(cf_token_is(&parser, "{"));
	assert_true(cf_pass_pair(&parser, '{', '}'));
	assert_true(cf_next_token(&parser));
	assert_true(cf_token_is(&parser, "w"));
	cf_parser_free(&parser);

	/* unbalanced pair stops at the terminator and fails */
	cf_parser_init(&parser);
	assert_true(cf_parser_parse(&parser, "{ x", "p.c"));
	assert_false(cf_pass_pair(&parser, '{', '}'));
	assert_int_equal(parser.cur_token->type, CFTOKEN_NONE);
	cf_parser_free(&parser);

	/* not on the opening char: no movement, success unless at the end */
	cf_parser_init(&parser);
	assert_true(cf_parser_parse(&parser, "q", "p.c"));
	struct cf_token *before = parser.cur_token;
	assert_true(cf_pass_pair(&parser, '{', '}'));
	assert_ptr_equal(parser.cur_token, before);
	cf_parser_free(&parser);

	assert_int_equal(bnum_allocs(), allocs);
}

static void parser_adderror_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_parser parser;
	cf_parser_init(&parser);

	assert_true(cf_parser_parse(&parser, "a\n  b", "p.c"));
	assert_true(cf_next_token(&parser));
	assert_true(cf_token_is(&parser, "b"));

	/* 'b' is on row 2, column 3 */
	cf_adderror_expecting(&parser, "x");
	cf_adderror_syntax_error(&parser);
	cf_adderror(&parser, "just a warning", LEX_WARNING, NULL, NULL, NULL);

	assert_int_equal(parser.error_list.errors.num, 3);
	assert_int_equal(error_data_type_count(&parser.error_list, LEX_ERROR), 2);
	assert_int_equal(error_data_type_count(&parser.error_list, LEX_WARNING), 1);
	assert_true(error_data_has_errors(&parser.error_list));

	const struct error_item *item = error_data_item(&parser.error_list, 0);
	assert_string_equal(item->error, "Expected 'x'");
	assert_string_equal(item->file, "p.c");
	assert_int_equal(item->row, 2);
	assert_int_equal(item->column, 3);
	assert_int_equal(item->level, LEX_ERROR);

	char *msg = error_data_buildstring(&parser.error_list);
	assert_string_equal(msg, "p.c (2, 3): Expected 'x'\np.c (2, 3): Syntax error\np.c (2, 3): just a warning\n");
	bfree(msg);

	cf_parser_free(&parser);
	assert_int_equal(bnum_allocs(), allocs);
}

static void parser_parse_failure_test(void **state)
{
	UNUSED_PARAMETER(state);

	long allocs = bnum_allocs();
	struct cf_parser parser;
	cf_parser_init(&parser);

	assert_false(cf_parser_parse(&parser, "", "p.c"));
	assert_null(parser.cur_token);
	/* unterminated block comment fails the lex step */
	assert_false(cf_parser_parse(&parser, "a /* b", "p.c"));

	cf_parser_free(&parser);
	assert_int_equal(bnum_allocs(), allocs);
}

int main()
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(literal_to_str_test),       cmocka_unit_test(lex_basic_test),
		cmocka_unit_test(lex_string_test),           cmocka_unit_test(lex_failure_test),
		cmocka_unit_test(preprocess_define_test),    cmocka_unit_test(preprocess_ifdef_test),
		cmocka_unit_test(preprocess_errors_test),    cmocka_unit_test(preprocess_add_remove_def_test),
		cmocka_unit_test(parser_pass_pair_test),     cmocka_unit_test(parser_adderror_test),
		cmocka_unit_test(parser_parse_failure_test),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
