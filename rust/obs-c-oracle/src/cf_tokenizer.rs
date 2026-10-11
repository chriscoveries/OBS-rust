//! `util/cf-tokenizer.c`, running on the lexer oracle.
use core::ffi::{c_char, c_int};

use super::darray::OracleDarray;
use super::lexer::{OracleLexer, OracleStrref};

/// Independent declaration of `struct cf_token`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct OracleCfToken {
    pub lex: *const OracleCfLexer,
    pub str: OracleStrref,
    pub unmerged_str: OracleStrref,
    pub kind: c_int,
}

/// Independent declaration of `struct cf_lexer`.
#[repr(C)]
#[derive(Debug)]
pub struct OracleCfLexer {
    pub file: *mut c_char,
    pub base_lexer: OracleLexer,
    pub reformatted: *mut c_char,
    pub write_offset: *mut c_char,
    pub tokens: OracleDarray,
    pub unexpected_eof: bool,
}

unsafe extern "C" {
    pub fn oracle_cf_literal_to_str(literal: *const c_char, count: usize) -> *mut c_char;
    pub fn oracle_cf_lexer_init(lex: *mut OracleCfLexer);
    pub fn oracle_cf_lexer_free(lex: *mut OracleCfLexer);
    pub fn oracle_cf_lexer_lex(
        lex: *mut OracleCfLexer,
        str: *const c_char,
        file: *const c_char,
    ) -> bool;

    pub fn oracle_cf_token_size() -> usize;
    pub fn oracle_cf_token_align() -> usize;
    pub fn oracle_cf_token_offset_lex() -> usize;
    pub fn oracle_cf_token_offset_str() -> usize;
    pub fn oracle_cf_token_offset_unmerged_str() -> usize;
    pub fn oracle_cf_token_offset_type() -> usize;
    pub fn oracle_cf_token_type_size() -> usize;
    pub fn oracle_cf_lexer_size() -> usize;
    pub fn oracle_cf_lexer_align() -> usize;
    pub fn oracle_cf_lexer_offset_file() -> usize;
    pub fn oracle_cf_lexer_offset_base_lexer() -> usize;
    pub fn oracle_cf_lexer_offset_reformatted() -> usize;
    pub fn oracle_cf_lexer_offset_write_offset() -> usize;
    pub fn oracle_cf_lexer_offset_tokens() -> usize;
    pub fn oracle_cf_lexer_offset_unexpected_eof() -> usize;
}
