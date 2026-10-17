#![cfg(windows)]
//! Tier 1: the safe core of the Windows branch of `utf8.c`
//! (`obs_util::platform::windows::utf8`), mirroring `test/cmocka/test_utf8_windows.c`.

use obs_c_oracle as _;
use obs_util::platform::windows::utf8::{utf8_to_wchar, wchar_to_utf8};

const SENTINEL: u16 = 0x5a5a;

/// Mirrors `test_round_trip`.
#[test]
fn round_trip() {
    let utf8 = "a\u{e9}\u{20ac}\u{1f600}".as_bytes();
    let mut wide = [0u16; 8];
    assert_eq!(utf8_to_wchar(utf8, None), 5);
    assert_eq!(utf8_to_wchar(utf8, Some(&mut wide)), 5);
    assert_eq!(&wide[..5], &[0x61, 0xe9, 0x20ac, 0xd83d, 0xde00]);

    let mut narrow = [0u8; 16];
    assert_eq!(wchar_to_utf8(&wide[..5], None), 10);
    assert_eq!(wchar_to_utf8(&wide[..5], Some(&mut narrow)), 10);
    assert_eq!(&narrow[..10], utf8);
}

/// Mirrors `test_lengths_and_terminators` (the NUL-terminated scan is the
/// shim's; the core converts what it is given, NULs included).
#[test]
fn lengths_and_terminators() {
    let mut wide = [SENTINEL; 8];
    assert_eq!(utf8_to_wchar(b"ab", Some(&mut wide)), 2);
    assert_eq!(wide[2], SENTINEL);

    assert_eq!(utf8_to_wchar(b"a\0b", Some(&mut wide)), 3);
    assert_eq!(&wide[..3], &[0x61, 0, 0x62]);
    assert_eq!(wchar_to_utf8(&[0x78, 0, 0x79], None), 3);
}

/// Mirrors `test_bom_dropped_and_flags_ignored` (the core has no flags).
#[test]
fn bom_dropped() {
    let mut wide = [0u16; 8];
    assert_eq!(utf8_to_wchar(b"\xef\xbb\xbfx", Some(&mut wide)), 1);
    assert_eq!(wide[0], 0x78);
    assert_eq!(utf8_to_wchar(b"x\xef\xbb\xbf", Some(&mut wide)), 2);
    assert_eq!(wide[1], 0xfeff);
    assert_eq!(utf8_to_wchar(b"\xef\xbb", Some(&mut wide)), 1);
    assert_eq!(utf8_to_wchar(b"\xef\xbb\xbf", Some(&mut wide)), 0);

    let mut narrow = [0u8; 8];
    assert_eq!(wchar_to_utf8(&[0xfeff, 0x78], Some(&mut narrow)), 4);
    assert_eq!(&narrow[..4], b"\xef\xbb\xbfx");
}

/// Mirrors `test_invalid_input_is_replaced`.
#[test]
fn invalid_input_is_replaced() {
    let mut wide = [0u16; 8];
    assert_eq!(utf8_to_wchar(b"a\xffb", Some(&mut wide)), 3);
    assert_eq!(wide[1], 0xfffd);

    let mut narrow = [0u8; 8];
    assert_eq!(wchar_to_utf8(&[0x78, 0xd800, 0x79], Some(&mut narrow)), 5);
    assert_eq!(&narrow[..5], b"x\xef\xbf\xbdy");
}

/// Mirrors `test_buffer_sizes` (a null buffer with a size is the shim's
/// case, in `utf8_windows_parity.rs`).
#[test]
fn buffer_sizes() {
    assert_eq!(utf8_to_wchar(b"abc", Some(&mut [0u16; 2])), 0);
    assert_eq!(wchar_to_utf8(&[0x61, 0x62, 0x63], Some(&mut [0u8; 2])), 0);

    assert_eq!(utf8_to_wchar(b"abc", Some(&mut [])), 3);
    assert_eq!(wchar_to_utf8(&[0x61, 0x62, 0x63], Some(&mut [])), 3);

    assert_eq!(utf8_to_wchar(b"", Some(&mut [0u16; 8])), 0);
    assert_eq!(wchar_to_utf8(&[], Some(&mut [0u8; 8])), 0);
}
