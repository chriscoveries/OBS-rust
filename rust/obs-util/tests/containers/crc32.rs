//! Tier 1: safe-core tests. The `test_crc32_*` tests mirror
//! `test/cmocka/test_crc32.c` case for case.

use obs_c_oracle as _; // links the test allocator
use obs_util::crc32::calc_crc32;

#[test]
fn test_crc32_check_value() {
    assert_eq!(calc_crc32(0, b"123456789"), 0xCBF4_3926);
}

#[test]
fn test_crc32_empty() {
    assert_eq!(calc_crc32(0, b""), 0);
    assert_eq!(calc_crc32(0x1234_5678, b""), 0x1234_5678);
}

#[test]
fn test_crc32_single_bytes() {
    assert_eq!(calc_crc32(0, b"a"), 0xE8B7_BE43);
    assert_eq!(calc_crc32(0, &[0u8]), 0xD202_EF8D);
}

#[test]
fn test_crc32_fox() {
    let fox = b"The quick brown fox jumps over the lazy dog";
    assert_eq!(fox.len(), 43);
    assert_eq!(calc_crc32(0, fox), 0x414F_A339);
}

#[test]
fn test_crc32_chaining() {
    assert_eq!(calc_crc32(calc_crc32(0, b"1234"), b"56789"), 0xCBF4_3926);
}

/// Bit-at-a-time reference CRC-32.
fn bitwise_crc32(crc: u32, buf: &[u8]) -> u32 {
    let mut crc = !crc;
    for &b in buf {
        crc ^= u32::from(b);
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    !crc
}

#[test]
fn every_single_byte_matches_bitwise_reference() {
    for b in 0..=255u8 {
        assert_eq!(calc_crc32(0, &[b]), bitwise_crc32(0, &[b]), "byte {b:#04x}");
    }
}
