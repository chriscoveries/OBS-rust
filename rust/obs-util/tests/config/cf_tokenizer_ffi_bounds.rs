#![cfg(unix)]

/* A nonzero count is the complete readable span for a counted C literal. */

use core::ffi::{CStr, c_char};
use core::ptr;

use obs_c_oracle as _;
use obs_util::ffi::cf_tokenizer::cf_literal_to_str;
use obs_util::ffi::darray::bfree;

struct GuardedLiteral {
    mapping: *mut libc::c_void,
    mapping_len: usize,
    literal: *const c_char,
}

impl GuardedLiteral {
    fn new(value: &[u8]) -> Self {
        assert!(!value.is_empty());

        // SAFETY: sysconf has no pointer arguments and returns the host page size.
        let page_size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
        assert!(page_size > 0);
        let page_size = page_size as usize;
        assert!(value.len() <= page_size);
        let mapping_len = 2 * page_size;

        // SAFETY: a two-page private anonymous mapping is valid with these flags.
        let mapping = unsafe {
            libc::mmap(
                ptr::null_mut(),
                mapping_len,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_PRIVATE | libc::MAP_ANON,
                -1,
                0,
            )
        };
        assert_ne!(mapping, libc::MAP_FAILED);

        // SAFETY: mapping is two pages long; the second page is inaccessible.
        let protected = unsafe {
            libc::mprotect(
                mapping.cast::<u8>().add(page_size).cast(),
                page_size,
                libc::PROT_NONE,
            )
        };
        assert_eq!(protected, 0);

        // SAFETY: the selected range is writable and ends at the guard page.
        let literal = unsafe {
            let destination = mapping.cast::<u8>().add(page_size - value.len());
            ptr::copy_nonoverlapping(value.as_ptr(), destination, value.len());
            destination.cast::<c_char>()
        };

        Self {
            mapping,
            mapping_len,
            literal,
        }
    }

    fn as_ptr(&self) -> *const c_char {
        self.literal
    }
}

impl Drop for GuardedLiteral {
    fn drop(&mut self) {
        // SAFETY: mapping is the live mapping returned by mmap.
        let result = unsafe { libc::munmap(self.mapping, self.mapping_len) };
        assert_eq!(result, 0);
    }
}

/// Converts `literal` through the C ABI with `count` equal to its length. The
/// literal ends at a guard page, so any read past `count` bytes faults.
fn convert_guarded(literal: &[u8]) -> Vec<u8> {
    let guarded = GuardedLiteral::new(literal);

    // SAFETY: guarded points to exactly literal.len() readable bytes, which
    // is the count passed.
    let output = unsafe { cf_literal_to_str(guarded.as_ptr(), literal.len()) };
    assert!(!output.is_null());

    // SAFETY: cf_literal_to_str returns a NUL-terminated bmalloc string.
    let output_bytes = unsafe { CStr::from_ptr(output) }.to_bytes().to_vec();

    // SAFETY: cf_literal_to_str allocates its result with bmalloc.
    unsafe { bfree(output.cast()) };
    output_bytes
}

#[test]
fn cf_literal_to_str_respects_nonzero_count() {
    assert_eq!(convert_guarded(b"\"a\""), b"a");
}

/* An escape whose skip lands on the end of the span makes the core index at
 * the span end (C's loop test reads `*temp_src` there). The index must read as
 * NUL rather than touch memory past `count`, so a slice one byte too long
 * faults here even though the plain literal above never reaches the end. */
#[test]
fn cf_literal_to_str_escape_at_span_end_stays_in_bounds() {
    assert_eq!(convert_guarded(b"\"a\\\""), b"a\"");
}
