//! Tier 3: the Rust C ABI shim behaves exactly like the original C,
//! compiled as an oracle. Includes buffers over 255 bytes, where both wrap
//! `pos` back to 0 (the C ABI keeps this; see `obs_util::bitstream`).
//!
//! Intentional differences from C: a null `buf` with `len > 0` reads as
//! empty in Rust; in C it dereferences NULL (undefined behavior). Not
//! generated here.

use core::ffi::c_int;

use obs_c_oracle::bitstream as c;
use obs_util::ffi::bitstream as rs;
use proptest::prelude::*;

#[derive(Debug, Clone)]
enum Op {
    ReadBits(c_int),
    R8,
    R16,
}

fn op() -> impl Strategy<Value = Op> {
    prop_oneof![
        (-2..=20 as c_int).prop_map(Op::ReadBits),
        Just(Op::R8),
        Just(Op::R16),
    ]
}

/// Runs `ops` on both implementations and compares every result and the
/// full reader state after each step.
fn check(data: Vec<u8>, len: usize, ops: &[Op]) -> Result<(), TestCaseError> {
    check_from(data, len, None, ops)
}

/// Like [`check`], but `start` overrides `(pos, subPos)` after init, so
/// mid-byte, odd, and zero `subPos` values (e.g. a zeroed struct) are covered.
fn check_from(
    mut data: Vec<u8>,
    len: usize,
    start: Option<(u8, u8)>,
    ops: &[Op],
) -> Result<(), TestCaseError> {
    let mut oracle_data = data.clone();
    let mut ours = rs::bitstream_reader {
        pos: 0,
        subPos: 0,
        buf: core::ptr::null_mut(),
        len: 0,
    };
    let mut theirs = c::OracleReader {
        pos: 0,
        subPos: 0,
        buf: core::ptr::null_mut(),
        len: 0,
    };

    // SAFETY: both readers point at live buffers of at least `len` bytes,
    // which outlive every call below.
    unsafe {
        rs::bitstream_reader_init(&mut ours, data.as_mut_ptr(), len);
        c::oracle_bitstream_reader_init(&mut theirs, oracle_data.as_mut_ptr(), len);
        if let Some((pos, sub_pos)) = start {
            ours.pos = pos;
            ours.subPos = sub_pos;
            theirs.pos = pos;
            theirs.subPos = sub_pos;
        }

        for (i, op) in ops.iter().enumerate() {
            let (a, b) = match *op {
                Op::ReadBits(n) => (
                    u16::from(rs::bitstream_reader_read_bits(&mut ours, n)),
                    u16::from(c::oracle_bitstream_reader_read_bits(&mut theirs, n)),
                ),
                Op::R8 => (
                    u16::from(rs::bitstream_reader_r8(&mut ours)),
                    u16::from(c::oracle_bitstream_reader_r8(&mut theirs)),
                ),
                Op::R16 => (
                    rs::bitstream_reader_r16(&mut ours),
                    c::oracle_bitstream_reader_r16(&mut theirs),
                ),
            };
            prop_assert_eq!(a, b, "result of op {} ({:?})", i, op);
            prop_assert_eq!(ours.pos, theirs.pos, "pos after op {}", i);
            prop_assert_eq!(ours.subPos, theirs.subPos, "subPos after op {}", i);
            prop_assert_eq!(ours.len, theirs.len);
        }
    }
    Ok(())
}

proptest! {
    #[test]
    fn matches_c_oracle(
        data in proptest::collection::vec(any::<u8>(), 0..=1024),
        len_cut in any::<prop::sample::Index>(),
        ops in proptest::collection::vec(op(), 0..=1200),
    ) {
        // Any len up to the buffer size, as the C test does with len = 5 of 6.
        let len = if data.is_empty() { 0 } else { len_cut.index(data.len() + 1) };
        check(data, len, &ops)?;
    }
}

proptest! {
    #[test]
    fn matches_c_oracle_from_any_state(
        data in proptest::collection::vec(any::<u8>(), 0..=300),
        pos in any::<u8>(),
        sub_pos in any::<u8>(),
        ops in proptest::collection::vec(op(), 0..=400),
    ) {
        let len = data.len();
        check_from(data, len, Some((pos, sub_pos)), &ops)?;
    }
}

/// Deterministic run straight across the 255 -> 0 wrap.
#[test]
fn wraps_like_c_past_byte_255() {
    let data: Vec<u8> = (0..300u32).map(|i| (i * 7 + 3) as u8).collect();
    let ops = vec![Op::R8; 600];
    check(data, 300, &ops).unwrap();
}
