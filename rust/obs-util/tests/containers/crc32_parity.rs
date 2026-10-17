//! Tier 3: the Rust C ABI shim and safe core behave exactly like the original
//! C, compiled as an oracle.
//!
//! No intentional differences from C.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use core::ffi::c_void;

use obs_c_oracle::crc32 as c;
use obs_util::crc32::calc_crc32;
use obs_util::ffi::crc32 as rs;
use proptest::prelude::*;

/// Tier 2: the shim accepts NULL with size 0.
#[test]
fn shim_accepts_null_with_zero_size() {
    // SAFETY: size is 0, so the buffer is never read.
    let (ours, theirs) = unsafe {
        (
            rs::calc_crc32(0x1234_5678, core::ptr::null(), 0),
            c::oracle_calc_crc32(0x1234_5678, core::ptr::null(), 0),
        )
    };
    assert_eq!(ours, 0x1234_5678);
    assert_eq!(theirs, 0x1234_5678);
}

proptest! {
    #[test]
    fn matches_c_oracle(
        crc in any::<u32>(),
        bytes in prop::collection::vec(any::<u8>(), 0..4096),
    ) {
        let ptr = bytes.as_ptr().cast::<c_void>();
        // SAFETY: `ptr` points to `bytes.len()` readable bytes that outlive both calls.
        let (ours, theirs) = unsafe {
            (
                rs::calc_crc32(crc, ptr, bytes.len()),
                c::oracle_calc_crc32(crc, ptr, bytes.len()),
            )
        };
        prop_assert_eq!(ours, theirs);
        prop_assert_eq!(calc_crc32(crc, &bytes), theirs);
    }

    #[test]
    fn chaining_equals_one_shot(
        crc in any::<u32>(),
        bytes in prop::collection::vec(any::<u8>(), 0..4096),
        split in any::<prop::sample::Index>(),
    ) {
        let at = split.index(bytes.len() + 1);
        let (a, b) = bytes.split_at(at);
        prop_assert_eq!(calc_crc32(calc_crc32(crc, a), b), calc_crc32(crc, &bytes));
    }
}
