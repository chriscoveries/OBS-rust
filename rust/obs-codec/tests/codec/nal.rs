//! Tier 1: safe-API tests mirroring `test/cmocka/test_nal.c`.

use obs_c_oracle as _;
use obs_codec::nal::find_startcode;
use proptest as _;

#[test]
fn three_byte_code_at_start() {
    assert_eq!(find_startcode(&[0, 0, 1, 0x65, 0xaa]), 0);
}

#[test]
fn four_byte_code_is_found_at_its_first_zero() {
    assert_eq!(find_startcode(&[0, 0, 0, 1, 0x67, 0xaa]), 0);
    assert_eq!(find_startcode(&[0x11, 0x22, 0, 0, 0, 1, 0x41]), 2);
}

#[test]
fn code_after_payload() {
    assert_eq!(find_startcode(&[0x11, 0x22, 0x33, 0, 0, 1, 0x41, 0x42]), 3);
}

#[test]
fn only_one_extra_zero_is_folded_in() {
    assert_eq!(find_startcode(&[0x11, 0, 0, 0, 0, 1, 0x41, 0x42]), 2);
}

#[test]
fn no_code_returns_len() {
    let data = [0x11, 0, 0x22, 0, 0, 0x33, 1, 0];
    assert_eq!(find_startcode(&data), data.len());
    assert_eq!(find_startcode(&[]), 0);
    assert_eq!(find_startcode(&[0, 0]), 2);
}

#[test]
fn code_must_not_end_the_buffer() {
    assert_eq!(find_startcode(&[0x11, 0x22, 0, 0, 1, 0x65]), 2);
    assert_eq!(find_startcode(&[0x11, 0x22, 0, 0, 1]), 5);
    assert_eq!(find_startcode(&[0, 0, 1]), 3);
}
