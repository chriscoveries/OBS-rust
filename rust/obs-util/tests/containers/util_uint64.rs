//! Tier 1: safe-core tests. The `test_*` tests mirror
//! `test/cmocka/test_util_uint64.c` case for case, including its split
//! between the MSVC x64 and other paths.

use obs_c_oracle as _; // links the test allocator
use obs_util::util_uint64::mul_div64;

/// `util_uint64.h` uses exact 128-bit math under this condition.
const EXACT: bool = cfg!(all(target_env = "msvc", target_arch = "x86_64"));

#[test]
fn test_audio_frames_to_ns() {
    assert_eq!(mul_div64(1024, 1_000_000_000, 48000), 21_333_333);
    assert_eq!(mul_div64(48000, 1_000_000_000, 48000), 1_000_000_000);
    assert_eq!(mul_div64(1, 1_000_000_000, 44100), 22675);
}

#[test]
fn test_ns_to_audio_frames() {
    assert_eq!(mul_div64(1_000_000_000, 48000, 1_000_000_000), 48000);
    assert_eq!(
        mul_div64(1_000_000_000_000_000_000, 48000, 1_000_000_000),
        48_000_000_000_000
    );
}

#[test]
fn test_frame_interval() {
    assert_eq!(mul_div64(1001, 1_000_000_000, 30000), 33_366_666);
}

#[test]
fn test_zero_operands() {
    assert_eq!(mul_div64(0, 123, 7), 0);
    assert_eq!(mul_div64(123, 0, 7), 0);
}

#[test]
fn test_div_one() {
    assert_eq!(mul_div64(7, 1, 1), 7);
    assert_eq!(mul_div64(u64::MAX, 1, 1), u64::MAX);
}

#[test]
fn test_large_values_without_overflow() {
    assert_eq!(mul_div64(u64::MAX, u64::MAX, u64::MAX), u64::MAX);
    assert_eq!(mul_div64(u64::MAX, 3, 3), u64::MAX);
    assert_eq!(mul_div64(u64::MAX, 2, 4), 0x7FFF_FFFF_FFFF_FFFF);
    assert_eq!(mul_div64(u64::MAX, 1_000_000_000, 1_000_000_000), u64::MAX);
    assert_eq!(
        mul_div64(3, 0x8000_0000_0000_0000, 2),
        0xC000_0000_0000_0000
    );
    assert_eq!(
        mul_div64(0x8000_0000_0000_0000, 2, 4),
        0x4000_0000_0000_0000
    );
}

#[test]
fn test_remainder_product_overflow() {
    // Characterized, not endorsed: off MSVC x64, rem * mul wraps first.
    if EXACT {
        assert_eq!(
            mul_div64(0x1_FFFF_FFFF, 0x1_0000_0000, 0x2_0000_0000),
            0xFFFF_FFFF
        );
        assert_eq!(mul_div64(5, u64::MAX, u64::MAX), 5);
        assert_eq!(mul_div64(u64::MAX - 1, u64::MAX, u64::MAX), u64::MAX - 1);
    } else {
        assert_eq!(
            mul_div64(0x1_FFFF_FFFF, 0x1_0000_0000, 0x2_0000_0000),
            0x7FFF_FFFF
        );
        assert_eq!(mul_div64(5, u64::MAX, u64::MAX), 0);
        assert_eq!(mul_div64(u64::MAX - 1, u64::MAX, u64::MAX), 0);
    }
}

#[cfg(not(all(target_env = "msvc", target_arch = "x86_64")))]
#[test]
fn test_quotient_overflow_wraps() {
    // Characterized, not endorsed.
    assert_eq!(mul_div64(0x8000_0000_0000_0000, 4, 1), 0);
    assert_eq!(mul_div64(u64::MAX, 2, 1), u64::MAX - 1);
}

#[test]
#[should_panic]
fn div_zero_panics() {
    // UB in C; the port panics rather than guess.
    mul_div64(1, 1, 0);
}

#[cfg(all(target_env = "msvc", target_arch = "x86_64"))]
#[test]
#[should_panic]
fn quotient_overflow_panics_on_msvc_x64() {
    // _udiv128 faults in C.
    mul_div64(u64::MAX, 2, 1);
}

#[test]
fn exact_whenever_remainder_product_fits() {
    // Both paths agree when rem * mul and the quotient fit in 64 bits.
    let cases = [
        (u64::MAX, u64::MAX, u64::MAX),
        (1u64 << 40, 1 << 30, (1 << 32) + 7),
        (123_456_789_012, 987_654, 1_000_003),
        (u64::MAX / 3, 2, 3),
    ];
    for (num, mul, div) in cases {
        let rem = num % div;
        assert!(rem.checked_mul(mul).is_some());
        let exact = u128::from(num) * u128::from(mul) / u128::from(div);
        assert_eq!(u128::from(mul_div64(num, mul, div)), exact);
    }
}
