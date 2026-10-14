/* The C ABI's bounded string comparators must read exactly n bytes: not past
 * n (guard-page tests, unix only), and not fewer (fixed-value tests that pin
 * both off-by-one directions, so no proptest seed is involved). */

use core::ffi::c_char;

// Links the C oracle, which supplies blog/bcrash and the other C symbols
// obs-util calls (as cf_tokenizer_ffi_bounds.rs does).
use obs_c_oracle as _;

use obs_util::ffi::dstr::{astrcmp_n, astrcmpi_n};

#[cfg(unix)]
mod guarded {
    use super::*;
    use core::ptr;

    struct GuardedBytes {
        mapping: *mut libc::c_void,
        mapping_len: usize,
        bytes: *const c_char,
    }

    impl GuardedBytes {
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

            // SAFETY: mapping is a two-page mapping; the second page is protected
            // before the test places the bytes at the end of the first page.
            let protected = unsafe {
                libc::mprotect(
                    mapping.cast::<u8>().add(page_size).cast(),
                    page_size,
                    libc::PROT_NONE,
                )
            };
            assert_eq!(protected, 0);

            // SAFETY: the first page remains writable and the selected range ends
            // exactly at its boundary.
            let bytes = unsafe {
                let destination = mapping.cast::<u8>().add(page_size - value.len());
                ptr::copy_nonoverlapping(value.as_ptr(), destination, value.len());
                destination.cast::<c_char>()
            };

            Self {
                mapping,
                mapping_len,
                bytes,
            }
        }

        fn as_ptr(&self) -> *const c_char {
            self.bytes
        }
    }

    impl Drop for GuardedBytes {
        fn drop(&mut self) {
            // SAFETY: mapping is the live mapping returned by mmap.
            let result = unsafe { libc::munmap(self.mapping, self.mapping_len) };
            assert_eq!(result, 0);
        }
    }

    #[test]
    fn astrcmp_n_does_not_scan_past_n() {
        let guarded = GuardedBytes::new(b"abc");
        let other = b"abc";

        // SAFETY: guarded has exactly three readable bytes before an inaccessible
        // page; other has at least three readable bytes; n is three.
        assert_eq!(
            unsafe { astrcmp_n(guarded.as_ptr(), other.as_ptr().cast(), 3) },
            0
        );
    }

    #[test]
    fn astrcmpi_n_does_not_scan_past_n() {
        let guarded = GuardedBytes::new(b"ABC");
        let other = b"abc";

        // SAFETY: guarded has exactly three readable bytes before an inaccessible
        // page; other has at least three readable bytes; n is three.
        assert_eq!(
            unsafe { astrcmpi_n(guarded.as_ptr(), other.as_ptr().cast(), 3) },
            0
        );
    }
}

/// Calls the bounded comparators through the C ABI on NUL-terminated inputs.
fn cmp_n(a: &[u8], b: &[u8], n: usize) -> i32 {
    assert_eq!(a.last(), Some(&0));
    assert_eq!(b.last(), Some(&0));
    // SAFETY: both inputs are NUL-terminated, so every read is in bounds.
    unsafe { astrcmp_n(a.as_ptr().cast(), b.as_ptr().cast(), n) }
}

fn cmpi_n(a: &[u8], b: &[u8], n: usize) -> i32 {
    assert_eq!(a.last(), Some(&0));
    assert_eq!(b.last(), Some(&0));
    // SAFETY: both inputs are NUL-terminated, so every read is in bounds.
    unsafe { astrcmpi_n(a.as_ptr().cast(), b.as_ptr().cast(), n) }
}

/* The byte at index n - 1 is the last one compared. A shim that compares only
 * n - 1 bytes returns 0 for these. */
#[test]
fn astrcmp_n_compares_the_nth_byte() {
    assert_eq!(cmp_n(b"abcd\0", b"abce\0", 4), -1);
    assert_eq!(cmp_n(b"abce\0", b"abcd\0", 4), 1);
    assert_eq!(cmp_n(b"a\0", b"b\0", 1), -1);
}

#[test]
fn astrcmpi_n_compares_the_nth_byte() {
    assert_eq!(cmpi_n(b"ABCD\0", b"abce\0", 4), -1);
    assert_eq!(cmpi_n(b"abce\0", b"ABCD\0", 4), 1);
    assert_eq!(cmpi_n(b"A\0", b"b\0", 1), -1);
}

/* The byte at index n is never compared. A shim that compares n + 1 bytes
 * returns nonzero for these, even though no memory fault occurs. */
#[test]
fn astrcmp_n_ignores_the_byte_after_n() {
    assert_eq!(cmp_n(b"abcX\0", b"abcY\0", 3), 0);
    assert_eq!(cmp_n(b"abcY\0", b"abcX\0", 3), 0);
}

#[test]
fn astrcmpi_n_ignores_the_byte_after_n() {
    assert_eq!(cmpi_n(b"ABCX\0", b"abcy\0", 3), 0);
    assert_eq!(cmpi_n(b"abcY\0", b"ABCx\0", 3), 0);
}

#[test]
fn bounded_compare_with_zero_n_reads_nothing() {
    // Never readable: a read through it faults, so any read fails the test.
    let unreadable = core::ptr::NonNull::<c_char>::dangling()
        .as_ptr()
        .cast_const();

    // SAFETY: n is 0, so neither pointer is dereferenced.
    unsafe {
        assert_eq!(astrcmp_n(unreadable, unreadable, 0), 0);
        assert_eq!(astrcmpi_n(unreadable, unreadable, 0), 0);
    }
}
