//! Tier 2: the `#[repr(C)]` mirrors of `struct cf_token` and
//! `struct cf_lexer` match the layouts the C compiler produces for
//! `libobs/util/cf-lexer.h`.

use core::ffi::c_int;
use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::cf_tokenizer as c;
use obs_util::ffi::cf_tokenizer::{cf_lexer, cf_token};

#[test]
fn cf_token_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(size_of::<cf_token>(), c::oracle_cf_token_size());
        assert_eq!(align_of::<cf_token>(), c::oracle_cf_token_align());
        assert_eq!(offset_of!(cf_token, lex), c::oracle_cf_token_offset_lex());
        assert_eq!(offset_of!(cf_token, str), c::oracle_cf_token_offset_str());
        assert_eq!(
            offset_of!(cf_token, unmerged_str),
            c::oracle_cf_token_offset_unmerged_str()
        );
        assert_eq!(
            offset_of!(cf_token, r#type),
            c::oracle_cf_token_offset_type()
        );
        assert_eq!(size_of::<c_int>(), c::oracle_cf_token_type_size());
    }
}

#[test]
fn cf_lexer_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<cf_lexer>(), c::oracle_cf_lexer_size());
        assert_eq!(align_of::<cf_lexer>(), c::oracle_cf_lexer_align());
        assert_eq!(offset_of!(cf_lexer, file), c::oracle_cf_lexer_offset_file());
        assert_eq!(
            offset_of!(cf_lexer, base_lexer),
            c::oracle_cf_lexer_offset_base_lexer()
        );
        assert_eq!(
            offset_of!(cf_lexer, reformatted),
            c::oracle_cf_lexer_offset_reformatted()
        );
        assert_eq!(
            offset_of!(cf_lexer, write_offset),
            c::oracle_cf_lexer_offset_write_offset()
        );
        assert_eq!(
            offset_of!(cf_lexer, tokens),
            c::oracle_cf_lexer_offset_tokens()
        );
        assert_eq!(
            offset_of!(cf_lexer, unexpected_eof),
            c::oracle_cf_lexer_offset_unexpected_eof()
        );
    }
}
