//! Tier 1: safe-core tests for `libobs/util/array-serializer.c`.

use obs_c_oracle as _;
use obs_util::array_serializer::{
    ArrayOutput, SeekType, WritePlan, plan_write, seek_target, seek_type_from_c,
};

/// 1:1 port of `test/cmocka/test_serializer.c`.
#[test]
fn serialize_test() {
    let mut out = ArrayOutput::new();
    for b in [0x01u8, 0xff, 0xe1] {
        assert_eq!(out.write(&[b]), 1);
    }
    assert_eq!(out.bytes(), &[0x01, 0xff, 0xe1]);
    assert_eq!(out.bytes().len(), 3);
    assert_eq!(out.get_pos(), 3);
}

#[test]
fn seek_type_conversion() {
    assert_eq!(seek_type_from_c(0), Some(SeekType::Start));
    assert_eq!(seek_type_from_c(1), Some(SeekType::Current));
    assert_eq!(seek_type_from_c(2), Some(SeekType::End));
    assert_eq!(seek_type_from_c(3), None);
    assert_eq!(seek_type_from_c(-1), None);
}

#[test]
fn seek_target_cases() {
    assert_eq!(seek_target(0, 5, 3, Some(SeekType::Start)), Some(3));
    assert_eq!(seek_target(0, 5, 6, Some(SeekType::Start)), None);
    assert_eq!(seek_target(0, 5, -1, Some(SeekType::Start)), None);
    assert_eq!(seek_target(4, 5, -1, Some(SeekType::Current)), Some(3));
    assert_eq!(seek_target(4, 5, 2, Some(SeekType::Current)), None);
    assert_eq!(seek_target(0, 5, 0, Some(SeekType::End)), Some(5));
    assert_eq!(seek_target(0, 5, 2, Some(SeekType::End)), Some(3));
    assert_eq!(seek_target(0, 5, 6, Some(SeekType::End)), None);
    assert_eq!(seek_target(4, 5, 99, None), Some(0));
}

#[test]
fn plan_write_cases() {
    assert_eq!(
        plan_write(0, 3, 2),
        WritePlan::Overwrite {
            offset: 0,
            new_num: 3
        }
    );
    assert_eq!(
        plan_write(2, 3, 5),
        WritePlan::Overwrite {
            offset: 2,
            new_num: 7
        }
    );
    assert_eq!(plan_write(3, 3, 2), WritePlan::Append);
    assert_eq!(plan_write(0, 0, 0), WritePlan::Append);
}

#[test]
fn overwrite_in_middle() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3, 4, 5]);
    assert_eq!(out.seek(1, Some(SeekType::Start)), 1);
    out.write(&[9, 9]);
    assert_eq!(out.bytes(), &[1, 9, 9, 4, 5]);
    assert_eq!(out.cur_pos(), 3);
    assert_eq!(out.get_pos(), 5);
}

#[test]
fn overwrite_extends_past_end() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    assert_eq!(out.seek(2, Some(SeekType::Start)), 2);
    out.write(&[7, 8, 9]);
    assert_eq!(out.bytes(), &[1, 2, 7, 8, 9]);
    assert_eq!(out.cur_pos(), 5);
    assert_eq!(out.get_pos(), 5);
}

#[test]
fn seek_end_and_current() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    assert_eq!(out.seek(0, Some(SeekType::Start)), 0);
    assert_eq!(out.seek(0, Some(SeekType::End)), 3);
    assert_eq!(out.cur_pos(), 3);
    assert_eq!(out.seek(-1, Some(SeekType::Current)), 2);
    assert_eq!(out.cur_pos(), 2);
}

#[test]
fn seek_beyond_num_fails_and_keeps_pos() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    assert_eq!(out.seek(1, Some(SeekType::Start)), 1);
    assert_eq!(out.seek(4, Some(SeekType::Start)), -1);
    assert_eq!(out.cur_pos(), 1);
}

#[test]
fn get_pos_is_len_not_cur_pos() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    out.seek(0, Some(SeekType::Start));
    assert_eq!(out.cur_pos(), 0);
    assert_eq!(out.get_pos(), 3);
}

#[test]
fn reset_clears_and_rewinds() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    out.reset();
    assert!(out.bytes().is_empty());
    assert_eq!(out.cur_pos(), 0);
    assert_eq!(out.get_pos(), 0);
    out.write(&[4]);
    assert_eq!(out.bytes(), &[4]);
}

#[test]
fn invalid_seek_kind_goes_to_zero() {
    let mut out = ArrayOutput::new();
    out.write(&[1, 2, 3]);
    assert_eq!(out.seek(2, None), 0);
    assert_eq!(out.cur_pos(), 0);
}

#[test]
fn empty_write_is_noop() {
    let mut out = ArrayOutput::new();
    assert_eq!(out.write(&[]), 0);
    assert!(out.bytes().is_empty());
    assert_eq!(out.cur_pos(), 0);
}
