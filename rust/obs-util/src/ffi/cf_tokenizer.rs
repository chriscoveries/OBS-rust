//! C ABI shim for the `EXPORT` functions of `libobs/util/cf-tokenizer.c`
//! (declared in `util/cf-lexer.h`).
//!
//! `cf_lexer_lex` allocates exactly what C does (`file`, the base lexer's
//! text, `reformatted` and the token darray, all through `bmalloc`) and
//! fills them from [`crate::cf_tokenizer::cf_lex`], so the C preprocessor
//! and parsers keep walking the same pointers.

use core::ffi::{CStr, c_char, c_int, c_ulong, c_void};
use core::ptr::{self, addr_of_mut};

use crate::cf_tokenizer as safe;
use crate::ffi::darray::{bfree, bmalloc, darray, darray_free, darray_push_back_array};
use crate::ffi::lexer::{lexer, strref};

/// Mirrors `struct cf_token`. `type` is `enum cf_token_type`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy)]
pub struct cf_token {
    pub lex: *const cf_lexer,
    pub str: strref,
    pub unmerged_str: strref,
    pub r#type: c_int,
}

/// Mirrors `struct cf_lexer`. `tokens` is `cf_token_array_t`, a
/// `DARRAY(struct cf_token)` with the layout of `struct darray`.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct cf_lexer {
    pub file: *mut c_char,
    pub base_lexer: lexer,
    pub reformatted: *mut c_char,
    pub write_offset: *mut c_char,
    pub tokens: darray,
    pub unexpected_eof: bool,
}

/// Copies `bytes` and a NUL into a new `bmalloc` buffer of `size` bytes.
///
/// # Safety
///
/// `size` must be greater than `bytes.len()`.
unsafe fn bmalloc_cstr(bytes: &[u8], size: usize) -> *mut c_char {
    debug_assert!(size > bytes.len());
    // SAFETY: bmalloc returns `size` bytes (it aborts on failure), enough
    // for `bytes` and the NUL; the buffers cannot overlap.
    unsafe {
        let p = bmalloc(size).cast::<u8>();
        ptr::copy_nonoverlapping(bytes.as_ptr(), p, bytes.len());
        *p.add(bytes.len()) = 0;
        p.cast()
    }
}

/// # Safety
///
/// `lex` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cf_lexer_init(lex: *mut cf_lexer) {
    // SAFETY: `lex` is valid for writes; every field is written.
    unsafe {
        lex.write(cf_lexer {
            file: ptr::null_mut(),
            base_lexer: lexer {
                text: ptr::null_mut(),
                offset: ptr::null(),
            },
            reformatted: ptr::null_mut(),
            write_offset: ptr::null_mut(),
            tokens: darray {
                array: ptr::null_mut(),
                num: 0,
                capacity: 0,
            },
            unexpected_eof: false,
        });
    }
}

/// # Safety
///
/// `lex` must point to a valid `cf_lexer` whose buffers came from `bmalloc`
/// (or are null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cf_lexer_free(lex: *mut cf_lexer) {
    // SAFETY: `lex` is valid and owns its buffers.
    unsafe {
        let l = &mut *lex;
        bfree(l.file.cast::<c_void>());
        bfree(l.reformatted.cast::<c_void>());
        // lexer_free: bfree the text, then zero the lexer.
        bfree(l.base_lexer.text.cast::<c_void>());
        l.base_lexer = lexer {
            text: ptr::null_mut(),
            offset: ptr::null(),
        };
        darray_free(addr_of_mut!(l.tokens));

        l.file = ptr::null_mut();
        l.reformatted = ptr::null_mut();
        l.write_offset = ptr::null_mut();
        l.unexpected_eof = false;
    }
}

/// Lexes `str` into `lex`, replacing what it held. Returns `false` for NULL
/// or empty input (with no tokens) and for an unterminated block comment.
///
/// # Safety
///
/// `lex` must point to a valid (initialized) `cf_lexer`; `str` and `file`
/// must be null or NUL-terminated strings.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cf_lexer_lex(
    lex: *mut cf_lexer,
    str: *const c_char,
    file: *const c_char,
) -> bool {
    // SAFETY: per the contract.
    unsafe { cf_lexer_free(lex) };
    if str.is_null() {
        return false;
    }
    // SAFETY: `str` is a NUL-terminated string.
    let input = unsafe { CStr::from_ptr(str) }.to_bytes();
    if input.is_empty() {
        return false;
    }

    let lexed = safe::cf_lex(input);
    assert!(lexed.reformatted.len() <= input.len());

    // SAFETY: `lex` is valid; every pointer stored below points into a
    // buffer this function allocates with room for what it addresses
    // (offsets reach at most one past the text's NUL, the end of its
    // allocation).
    unsafe {
        let l = &mut *lex;
        if !file.is_null() {
            let f = CStr::from_ptr(file).to_bytes();
            l.file = bmalloc_cstr(f, f.len() + 1);
        }

        // lexer_start: bstrdup the text.
        let text = bmalloc_cstr(input, input.len() + 1);
        l.base_lexer.text = text;
        l.base_lexer.offset = text.add(lexed.offset);

        l.reformatted = bmalloc_cstr(&lexed.reformatted, input.len() + 1);
        l.write_offset = l.reformatted.add(lexed.reformatted.len());

        for t in &lexed.tokens {
            let token = cf_token {
                lex,
                str: strref {
                    array: l.reformatted.add(t.str_start),
                    len: t.str_len,
                },
                unmerged_str: strref {
                    array: text.add(t.unmerged_start),
                    len: t.unmerged_len,
                },
                r#type: t.kind as c_int,
            };
            darray_push_back_array(
                size_of::<cf_token>(),
                addr_of_mut!(l.tokens),
                ptr::from_ref(&token).cast::<c_void>(),
                1,
            );
        }

        l.unexpected_eof = lexed.unexpected_eof;
        !l.unexpected_eof
    }
}

/// Returns a `bzalloc` copy of the literal without its quotes and with its
/// escapes converted, or NULL if `literal` is not a quoted literal.
///
/// Intentional difference: a NULL `literal` returns NULL (C dereferences
/// it).
///
/// # Safety
///
/// `literal` must be null, or readable for `count` bytes when `count` is
/// nonzero. With `count` 0 it must be a NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cf_literal_to_str(literal: *const c_char, count: usize) -> *mut c_char {
    if literal.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: count == 0 follows the C-string contract; otherwise the caller
    // guarantees the counted span is readable.
    let bytes = unsafe {
        if count == 0 {
            CStr::from_ptr(literal).to_bytes()
        } else {
            core::slice::from_raw_parts(literal.cast::<u8>(), count)
        }
    };

    match safe::cf_literal_to_str(bytes, count, c_ulong::BITS) {
        None => ptr::null_mut(),
        // SAFETY: bmalloc returns `out.len()` bytes; `out` is the whole
        // zero-filled buffer C's bzalloc would hold.
        Some(out) => unsafe {
            let p = bmalloc(out.len()).cast::<u8>();
            ptr::copy_nonoverlapping(out.as_ptr(), p, out.len());
            p.cast()
        },
    }
}
