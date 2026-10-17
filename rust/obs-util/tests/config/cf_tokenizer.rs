//! Tier 1: safe-core tests. The `*_test` tests mirror the tokenizer cases
//! of `test/cmocka/test_cf_lexer.c` (`literal_to_str_test`, `lex_*_test`);
//! its preprocessor and parser cases cover code that stays in C.
//!
//! NULL and empty input, `lex->file`, the token `lex` back-pointers and the
//! allocation counts are C-side; `cf_tokenizer_parity.rs` covers them at
//! the shim.

use obs_c_oracle as _; // links the test allocator
use obs_util::cf_tokenizer::{CfLexed, CfTokenType, cf_lex, cf_literal_to_str};

const ULONG: u32 = core::ffi::c_ulong::BITS;

/// `cf_literal_to_str` as a C string: the bytes before the first NUL.
fn lit(literal: &[u8], count: usize) -> Option<Vec<u8>> {
    cf_literal_to_str(literal, count, ULONG).map(|mut v| {
        v.truncate(v.iter().position(|&b| b == 0).unwrap_or(v.len()));
        v
    })
}

fn tokens(lexed: &CfLexed) -> Vec<(CfTokenType, &[u8])> {
    lexed
        .tokens
        .iter()
        .map(|t| {
            (
                t.kind,
                &lexed.reformatted[t.str_start..t.str_start + t.str_len],
            )
        })
        .collect()
}

#[test]
fn literal_to_str_test() {
    assert_eq!(lit(b"\"abc\"", 0).as_deref(), Some(&b"abc"[..]));

    // single-quoted
    assert_eq!(lit(b"'xy'", 0).as_deref(), Some(&b"xy"[..]));

    // explicit count shorter than the text
    assert_eq!(lit(b"\"abc\"zzz", 5).as_deref(), Some(&b"abc"[..]));

    // escapes consume several chars, yet copying still stops at the
    // closing quote
    assert_eq!(lit(b"\"a\\nb\"", 0).as_deref(), Some(&b"a\nb"[..]));
    assert_eq!(lit(b"\"\\t\"", 0).as_deref(), Some(&b"\t"[..]));

    // invalid literals
    assert_eq!(lit(b"abc", 0), None);
    assert_eq!(lit(b"\"abc'", 0), None);
    assert_eq!(lit(b"\"", 0), None);
    assert_eq!(lit(b"\"abcdef\"", 5), None);

    // empty quotes
    assert_eq!(lit(b"\"\"", 0).as_deref(), Some(&b""[..]));
}

#[test]
fn lex_basic_test() {
    use CfTokenType::*;

    let lexed = cf_lex(b"int x = 5; // c\n/* b */ float y;");
    assert!(!lexed.unexpected_eof);

    // comments are reduced to a single space, which merges with adjacent
    // space/tab tokens
    assert_eq!(lexed.reformatted, b"int x = 5;  \n  float y;");

    let expected: &[(CfTokenType, &[u8])] = &[
        (Name, b"int"),
        (SpaceTab, b" "),
        (Name, b"x"),
        (SpaceTab, b" "),
        (Other, b"="),
        (SpaceTab, b" "),
        (Num, b"5"),
        (Other, b";"),
        (SpaceTab, b"  "),
        (Newline, b"\n"),
        (SpaceTab, b"  "),
        (Name, b"float"),
        (SpaceTab, b" "),
        (Name, b"y"),
        (Other, b";"),
        // terminator
        (None, b""),
    ];
    assert_eq!(tokens(&lexed), expected);
}

#[test]
fn lex_string_test() {
    use CfTokenType::*;

    let lexed = cf_lex(b"x = \"hi there\";");
    let expected: &[(CfTokenType, &[u8])] = &[
        (Name, b"x"),
        (SpaceTab, b" "),
        (Other, b"="),
        (SpaceTab, b" "),
        (String, b"\"hi there\""),
        (Other, b";"),
        (None, b""),
    ];
    assert_eq!(tokens(&lexed), expected);
}

#[test]
fn lex_failure_test() {
    // empty / NULL input: shim only (no tokens are produced)

    // unterminated block comment
    assert!(cf_lex(b"a /* b").unexpected_eof);
}

// Edge cases beyond the C test. All are characterized from the C code.

#[test]
fn splices_are_merged() {
    use CfTokenType::*;

    let lexed = cf_lex(b"ab\\\ncd \\\r\n+");
    assert_eq!(lexed.reformatted, b"abcd +");
    assert_eq!(
        tokens(&lexed),
        [
            (Name, &b"abcd"[..]),
            (SpaceTab, b" "),
            (Other, b"+"),
            (None, b"")
        ]
    );
    // unmerged_str spans the splice in the original text
    assert_eq!(lexed.tokens[0].unmerged_len, 6);
}

#[test]
fn numbers_and_names() {
    use CfTokenType::*;

    let lexed = cf_lex(b"1.5e3 .5 _a1 12ab x.y");
    assert_eq!(
        tokens(&lexed),
        [
            (Num, &b"1.5e3"[..]),
            (SpaceTab, b" "),
            (Num, b".5"),
            (SpaceTab, b" "),
            (Name, b"_a1"),
            (SpaceTab, b" "),
            (Num, b"12ab"),
            (SpaceTab, b" "),
            (Name, b"x"),
            (Other, b"."),
            (Name, b"y"),
            (None, b""),
        ]
    );
}

#[test]
fn newlines_are_not_merged() {
    use CfTokenType::*;

    let lexed = cf_lex(b"a\r\n\n\tb");
    assert_eq!(
        tokens(&lexed),
        [
            (Name, &b"a"[..]),
            (Newline, b"\r\n"),
            (Newline, b"\n"),
            (SpaceTab, b"\t"),
            (Name, b"b"),
            (None, b""),
        ]
    );
}

#[test]
fn string_quirks() {
    use CfTokenType::*;

    // escaped delimiters stay inside the string
    let lexed = cf_lex(b"'a\\'b'");
    assert_eq!(tokens(&lexed)[0], (String, &b"'a\\'b'"[..]));
    // characterized, not endorsed: unmerged_str is one byte too long
    assert_eq!(lexed.tokens[0].unmerged_len, 7);

    // an unterminated string stops at the newline
    let lexed = cf_lex(b"\"ab\ncd");
    assert_eq!(
        tokens(&lexed)[..2],
        [(String, &b"\"ab"[..]), (Newline, b"\n")]
    );
}

#[test]
fn include_strings() {
    use CfTokenType::*;

    // after #include, <...> is a string and backslashes do not escape
    let lexed = cf_lex(b"#include <a\\b.h>\n#include \"c\\\"\n");
    let t = tokens(&lexed);
    assert_eq!(t[3], (String, &b"<a\\b.h>"[..]));
    assert_eq!(t[8], (String, &b"\"c\\\""[..]));

    // elsewhere '<' is an operator
    let lexed = cf_lex(b"a <b>");
    assert_eq!(tokens(&lexed)[2], (Other, &b"<"[..]));

    // #include in the middle of a line does not count
    let lexed = cf_lex(b"x #include <y>");
    assert_eq!(tokens(&lexed)[5], (Other, &b"<"[..]));
}

#[test]
fn comments() {
    // a line comment keeps its newline; a block comment may span lines
    let lexed = cf_lex(b"a//x\nb/*1\n2*/c");
    assert_eq!(lexed.reformatted, b"a \nb c");
    assert!(!lexed.unexpected_eof);
    // a comment start split by a splice is still a comment
    let lexed = cf_lex(b"a/\\\n/x");
    assert_eq!(lexed.reformatted, b"a ");
    // "/*/" is not closed by its own star
    assert!(cf_lex(b"/*/").unexpected_eof);
    // the flag is reset by a later closed comment only if it was this one
    assert!(cf_lex(b"/**/ /*").unexpected_eof);
}

#[test]
fn terminator_points_at_the_end() {
    let lexed = cf_lex(b"ab");
    let end = lexed.tokens.last().unwrap();
    assert_eq!((end.str_start, end.unmerged_start), (2, 2));
    assert_eq!(lexed.offset, 2);
}

#[test]
fn literal_escapes() {
    let all = b"\"\\'\\\"\\?\\\\\\a\\b\\f\\n\\r\\t\\v\"";
    // one output char per escape, then the zero fill
    assert_eq!(&lit(all, 0).unwrap()[..11], b"'\"?\\\x07\x08\x0c\n\r\t\x0b");

    // \0 ends the C string
    assert_eq!(lit(b"\"a\\0b\"", 0).as_deref(), Some(&b"a"[..]));

    // \x reads every hex digit strtoul takes, then skips exactly two
    assert_eq!(
        cf_literal_to_str(b"\"\\x41BC\"", 0, ULONG).unwrap()[0],
        0xBC
    );
    assert_eq!(lit(b"\"\\x41\"", 0).as_deref(), Some(&b"A"[..]));

    // octal starts at the second digit: "\101" parses "01"
    assert_eq!(cf_literal_to_str(b"\"\\101\"", 0, ULONG).unwrap()[0], 0o1);
    // an unknown escape writes nothing
    assert_eq!(lit(b"\"\\qz\"", 0).as_deref(), Some(&b"z"[..]));
}

#[test]
fn literal_hex_overflow_depends_on_unsigned_long() {
    // 9 hex digits overflow a 32-bit unsigned long but not a 64-bit one.
    let s = b"\"\\x123456789\"";
    assert_eq!(cf_literal_to_str(s, 0, 32).unwrap()[0], 0xFF);
    assert_eq!(cf_literal_to_str(s, 0, 64).unwrap()[0], 0x89);
    // strtoul accepts a sign and a 0x prefix
    assert_eq!(cf_literal_to_str(b"\"\\x-1\"", 0, 32).unwrap()[0], 0xFF);
    assert_eq!(cf_literal_to_str(b"\"\\x0x7f\"", 0, 32).unwrap()[0], 0x7F);
}

#[test]
fn literal_buffer_is_count_minus_one_zero_filled() {
    let out = cf_literal_to_str(b"'ab'", 0, ULONG).unwrap();
    assert_eq!(out, b"ab\0");
}
