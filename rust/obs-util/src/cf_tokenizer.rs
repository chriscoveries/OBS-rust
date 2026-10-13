//! Safe core for `libobs/util/cf-tokenizer.c`: the C-family lexer
//! (`cf_lexer_lex`) and `cf_literal_to_str`.
//!
//! The lexer turns source text into a reformatted copy, with spliced lines
//! (backslash-newline) merged and comments replaced by one space, and into
//! tokens that point both into that copy (`str`) and into the original text
//! (`unmerged_str`). Tokens here are offsets; the shim turns them into the
//! C pointers the preprocessor and parsers walk.
//!
//! Text is read as if NUL bytes followed it. That matches C wherever C
//! stays in bounds. C reads past the terminator when a comment or string
//! ends in a line splice at the very end of the text (and `\x`/octal escapes
//! in `cf_literal_to_str` can skip past it); there the Rust port reads NUL
//! where C reads whatever follows.

use crate::lexer::{BaseTokenType, IgnoreWhitespace, scan_base_token, strref_cmp};

/// `enum cf_token_type`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum CfTokenType {
    None = 0,
    Name = 1,
    Num = 2,
    SpaceTab = 3,
    Newline = 4,
    String = 5,
    Other = 6,
}

/// A `struct cf_token` as offsets: `str` into [`CfLexed::reformatted`],
/// `unmerged` into the input text.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CfToken {
    pub str_start: usize,
    pub str_len: usize,
    pub unmerged_start: usize,
    pub unmerged_len: usize,
    pub kind: CfTokenType,
}

/// The result of [`cf_lex`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CfLexed {
    /// The reformatted text, without its NUL terminator.
    pub reformatted: Vec<u8>,
    /// The tokens, ending with a `CfTokenType::None` terminator whose `str`
    /// is the end of `reformatted` and whose `unmerged` is `offset`.
    pub tokens: Vec<CfToken>,
    /// The base lexer's final offset into the input.
    pub offset: usize,
    /// An unterminated block comment was found.
    pub unexpected_eof: bool,
}

fn is_newline(b: u8) -> bool {
    matches!(b, b'\r' | b'\n')
}

fn is_space_or_tab(b: u8) -> bool {
    matches!(b, b' ' | b'\t')
}

fn is_newline_pair(a: u8, b: u8) -> bool {
    (a == b'\r' && b == b'\n') || (a == b'\n' && b == b'\r')
}

struct Lexer<'a> {
    text: &'a [u8],
    offset: usize,
    out: Vec<u8>,
    tokens: Vec<CfToken>,
    unexpected_eof: bool,
}

impl Lexer<'_> {
    fn at(&self, i: usize) -> u8 {
        self.text.get(i).copied().unwrap_or(0)
    }

    /// `newline_size`.
    fn newline_size(&self, i: usize) -> usize {
        if is_newline_pair(self.at(i), self.at(i + 1)) {
            2
        } else {
            usize::from(is_newline(self.at(i)))
        }
    }

    /// `cf_is_splice`.
    fn is_splice(&self, i: usize) -> bool {
        self.at(i) == b'\\' && is_newline(self.at(i + 1))
    }

    /// `cf_pass_any_splices`.
    fn pass_splices(&self, mut i: usize) -> usize {
        while self.is_splice(i) {
            i += 1 + self.newline_size(i + 1);
        }
        i
    }

    /// `cf_is_comment`.
    fn is_comment(&self, i: usize) -> bool {
        self.at(i) == b'/' && matches!(self.at(self.pass_splices(i + 1)), b'*' | b'/')
    }

    /// The first reformatted byte of a token (`*token->str.array`).
    fn first(&self, token: &CfToken) -> u8 {
        self.out.get(token.str_start).copied().unwrap_or(0)
    }

    /// `cf_lexer_process_comment`.
    fn process_comment(&mut self, out: &mut CfToken) -> bool {
        if !self.is_comment(out.unmerged_start) {
            return false;
        }

        let mut off = self.pass_splices(self.offset);
        self.out.push(b' ');
        out.str_len = 1;

        if self.at(off) == b'/' {
            loop {
                off += 1;
                let c = self.at(off);
                if c == 0 || is_newline(c) {
                    break;
                }
                off = self.pass_splices(off);
            }
        } else if self.at(off) == b'*' {
            let mut was_star = false;
            self.unexpected_eof = true;
            loop {
                off += 1;
                if self.at(off) == 0 {
                    break;
                }
                off = self.pass_splices(off);
                if was_star && self.at(off) == b'/' {
                    off += 1;
                    self.unexpected_eof = false;
                    break;
                }
                was_star = self.at(off) == b'*';
            }
        }

        out.unmerged_len += off - out.unmerged_start;
        out.kind = CfTokenType::SpaceTab;
        self.offset = off;
        true
    }

    /// `cf_lexer_is_include`: the tokens so far end in `#include` or
    /// `#import` at the start of a line.
    fn is_include(&self) -> bool {
        let (mut found_include_import, mut found_preprocessor) = (false, false);

        for token in self.tokens.iter().rev() {
            let first = self.first(token);
            if is_space_or_tab(first) {
                continue;
            }

            if !found_include_import {
                let s = self
                    .out
                    .get(token.str_start..token.str_start + token.str_len)
                    .unwrap_or(&[]);
                if strref_cmp(Some(s), Some(b"include")) != 0
                    && strref_cmp(Some(s), Some(b"import")) != 0
                {
                    break;
                }
                found_include_import = true;
            } else if !found_preprocessor {
                if first != b'#' {
                    break;
                }
                found_preprocessor = true;
            } else {
                return is_newline(first);
            }
        }

        found_preprocessor && found_include_import
    }

    /// `cf_lexer_getstrtoken`.
    fn get_str_token(&mut self, out: &mut CfToken, delimiter: u8, allow_escaped: bool) {
        let mut off = self.offset;
        let mut escaped = false;

        out.unmerged_len += 1;
        out.str_len += 1;
        self.out.push(self.at(out.unmerged_start));

        while self.at(off) != 0 {
            off = self.pass_splices(off);
            let c = self.at(off);
            if c == delimiter {
                if !escaped {
                    self.out.push(c);
                    out.str_len += 1;
                    off += 1;
                    break;
                }
            } else if is_newline(c) {
                break;
            }

            self.out.push(c);
            out.str_len += 1;
            escaped = allow_escaped && c == b'\\';
            off += 1;
        }

        // Characterized, not endorsed: the opening quote is counted twice,
        // so unmerged_str is one byte longer than the string.
        out.unmerged_len += off - out.unmerged_start;
        out.kind = CfTokenType::String;
        self.offset = off;
    }

    /// `cf_lexer_process_string`.
    fn process_string(&mut self, out: &mut CfToken) -> bool {
        let ch = self.at(out.unmerged_start);
        if ch == b'<' && self.is_include() {
            self.get_str_token(out, b'>', false);
            true
        } else if ch == b'"' || ch == b'\'' {
            let allow_escaped = !self.is_include();
            self.get_str_token(out, ch, allow_escaped);
            true
        } else {
            false
        }
    }

    /// `cf_is_token_break`. May turn a leading `.` into a number.
    fn is_token_break(
        &self,
        start: &mut (usize, BaseTokenType),
        token: (usize, BaseTokenType),
    ) -> bool {
        use BaseTokenType as B;
        match start.1 {
            B::Alpha => matches!(token.1, B::Other | B::Whitespace),
            B::Digit => {
                token.1 == B::Whitespace || (token.1 == B::Other && self.at(token.0) != b'.')
            }
            B::Whitespace => {
                !(is_space_or_tab(self.at(start.0)) && is_space_or_tab(self.at(token.0)))
            }
            B::Other if self.at(start.0) == b'.' && token.1 == B::Digit => {
                start.1 = B::Digit;
                false
            }
            B::Other | B::None => true,
        }
    }

    /// `cf_lexer_nexttoken`.
    fn next_token(&mut self) -> Option<CfToken> {
        let mut out = CfToken {
            str_start: 0,
            str_len: 0,
            unmerged_start: 0,
            unmerged_len: 0,
            kind: CfTokenType::None,
        };
        let mut start: Option<(usize, BaseTokenType)> = None;

        loop {
            let rest = self.text.get(self.offset..).unwrap_or(&[]);
            let (advance, base) = scan_base_token(
                |i| rest.get(i).copied().unwrap_or(0),
                IgnoreWhitespace::Parse,
            );
            let token_offset = self.offset;
            self.offset += advance;
            let Some(base) = base else {
                break;
            };

            let token_start = token_offset + base.start;
            let mut kind = base.kind;
            // reclassify underscore as alpha for alnum tokens
            if self.at(token_start) == b'_' {
                kind = BaseTokenType::Alpha;
            }

            // ignore escaped newlines to merge spliced lines
            if self.is_splice(token_start) {
                self.offset += self.newline_size(token_start + 1);
                continue;
            }

            match &mut start {
                None => {
                    out.unmerged_start = token_start;
                    out.str_start = self.out.len();
                    if self.process_comment(&mut out) || self.process_string(&mut out) {
                        return Some(out);
                    }
                    start = Some((token_start, kind));
                }
                Some(st) => {
                    let mut st_copy = *st;
                    let brk = self.is_token_break(&mut st_copy, (token_start, kind));
                    *st = st_copy;
                    if brk {
                        self.offset -= base.len;
                        break;
                    }
                }
            }

            self.out
                .extend_from_slice(&self.text[token_start..token_start + base.len]);
            out.str_len += base.len;
        }

        let (_, start_kind) = start?;
        out.unmerged_len = self.offset - out.unmerged_start;
        out.kind = match start_kind {
            BaseTokenType::Alpha => CfTokenType::Name,
            BaseTokenType::Digit => CfTokenType::Num,
            BaseTokenType::Whitespace if is_newline(self.first(&out)) => CfTokenType::Newline,
            BaseTokenType::Whitespace => CfTokenType::SpaceTab,
            BaseTokenType::None | BaseTokenType::Other => CfTokenType::Other,
        };
        Some(out)
    }
}

/// `cf_lexer_lex` on non-empty `text` (the input up to its NUL).
///
/// Adjacent space/tab tokens, comments included, are merged into one.
pub fn cf_lex(text: &[u8]) -> CfLexed {
    let mut lex = Lexer {
        text,
        offset: 0,
        out: Vec::with_capacity(text.len()),
        tokens: Vec::new(),
        unexpected_eof: false,
    };

    while let Some(token) = lex.next_token() {
        if let Some(last) = lex.tokens.last()
            && is_space_or_tab(lex.first(last))
            && is_space_or_tab(lex.first(&token))
        {
            let last = lex.tokens.last_mut().expect("checked above");
            last.str_len += token.str_len;
            last.unmerged_len += token.unmerged_len;
            continue;
        }
        lex.tokens.push(token);
    }

    lex.tokens.push(CfToken {
        str_start: lex.out.len(),
        str_len: 0,
        unmerged_start: lex.offset,
        unmerged_len: 0,
        kind: CfTokenType::None,
    });

    CfLexed {
        reformatted: lex.out,
        tokens: lex.tokens,
        offset: lex.offset,
        unexpected_eof: lex.unexpected_eof,
    }
}

/// C `isspace` in the C locale.
fn is_c_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

/// The low byte of `strtoul(at(i..), NULL, base)`, the way
/// `(char)strtoul(...)` keeps it, with `unsigned long` of `ulong_bits`
/// bits: leading whitespace, a sign, and for base 16 a `0x` prefix are
/// accepted; overflow gives `ULONG_MAX`.
fn strtoul_low_byte(at: impl Fn(usize) -> u8, mut i: usize, base: u32, ulong_bits: u32) -> u8 {
    let max = if ulong_bits >= 64 {
        u64::MAX
    } else {
        (1u64 << ulong_bits) - 1
    };

    while is_c_space(at(i)) {
        i += 1;
    }
    let negative = at(i) == b'-';
    if matches!(at(i), b'+' | b'-') {
        i += 1;
    }
    if base == 16 && at(i) == b'0' && matches!(at(i + 1), b'x' | b'X') {
        i += 2;
    }

    let mut value: u64 = 0;
    let mut overflow = false;
    while let Some(digit) = char::from(at(i)).to_digit(base) {
        match value
            .checked_mul(u64::from(base))
            .and_then(|v| v.checked_add(u64::from(digit)))
        {
            Some(v) if v <= max => value = v,
            _ => overflow = true,
        }
        i += 1;
    }

    let result = if overflow {
        max
    } else if negative {
        value.wrapping_neg() & max
    } else {
        value
    };
    result as u8
}

/// `cf_literal_to_str`: strips the quotes from a `"..."` or `'...'` literal
/// and converts its escape sequences.
///
/// `literal` holds only the bytes the caller supplied: the `count` bytes of
/// the literal when `count` is nonzero (the FFI passes exactly that counted
/// slice, with no trailing NUL needed), or the string up to its NUL when
/// `count` is 0. Any index at or past the end of `literal` reads as NUL, so
/// this never looks beyond the supplied bytes.
///
/// C differs only in that read. Its loop tests `*temp_src` before
/// `temp_src < end` (`cf-tokenizer.c:110`), so when an escape ends at the
/// closing quote and its skip leaves `temp_src` at or past `end`, C reads one
/// byte outside the counted span. That value is discarded, because the second
/// conjunct is already false, so the output is identical; `strtoul` in the
/// `\x` and octal escapes cannot cross the span, since it starts before the
/// closing quote and the quote stops it. `ulong_bits` is the width of C
/// `unsigned long`, which decides when `\x` and octal escapes overflow to
/// 0xff.
///
/// Returns the zero-filled `count - 1` byte buffer C allocates, or `None`
/// if the text is not a quoted literal. Copying stops at the closing quote
/// (an escape consumes several input bytes but writes at most one, so the
/// result always fits). Characterized, not endorsed: `\x` consumes as many hex digits as
/// `strtoul` takes but skips exactly two, and an octal escape starts
/// parsing at its second digit and skips three.
pub fn cf_literal_to_str(literal: &[u8], count: usize, ulong_bits: u32) -> Option<Vec<u8>> {
    let at = |i: usize| literal.get(i).copied().unwrap_or(0);
    let n = if count == 0 {
        literal
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(literal.len())
    } else {
        count
    };

    if n < 2 || at(0) != at(n - 1) || !matches!(at(0), b'"' | b'\'') {
        return None;
    }

    let end = n - 1; // the closing quote
    let mut out = vec![0u8; n - 1];
    let (mut src, mut dst) = (1, 0);

    // C: while (*temp_src && temp_src < end)
    while at(src) != 0 && src < end {
        if at(src) != b'\\' {
            out[dst] = at(src);
            dst += 1;
            src += 1;
            continue;
        }

        // cf_convert_from_escape_literal
        let esc = at(src + 1);
        src += 2;
        let simple = match esc {
            b'\'' => Some(b'\''),
            b'"' => Some(b'"'),
            b'?' => Some(b'?'),
            b'\\' => Some(b'\\'),
            b'0' => Some(0),
            b'a' => Some(0x07),
            b'b' => Some(0x08),
            b'f' => Some(0x0c),
            b'n' => Some(b'\n'),
            b'r' => Some(b'\r'),
            b't' => Some(b'\t'),
            b'v' => Some(0x0b),
            b'x' | b'X' => {
                let v = strtoul_low_byte(at, src, 16, ulong_bits);
                src += 2;
                Some(v)
            }
            _ if at(src).is_ascii_digit() => {
                let v = strtoul_low_byte(at, src, 8, ulong_bits);
                src += 3;
                Some(v)
            }
            _ => None,
        };
        if let Some(b) = simple {
            out[dst] = b;
            dst += 1;
        }
    }

    out[dst] = 0;
    Some(out)
}
