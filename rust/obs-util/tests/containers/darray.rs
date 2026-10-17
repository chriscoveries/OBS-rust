//! Tier 1: safe-core tests. `array_basic_test` mirrors
//! `test/cmocka/test_darray.c`.

use obs_c_oracle as _;
use obs_util::darray::{DARRAY_INVALID, DArray, grow_capacity};

#[test]
fn array_basic_test() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[1]);
    assert_eq!(da.len(), 1);
    assert_eq!(da.as_slice(), [1]);
    da.free();
    assert_eq!(da.len(), 0);
    assert_eq!(da.capacity(), 0);
}

#[test]
fn grow_capacity_matches_header() {
    assert_eq!(grow_capacity(0, 5), 5);
    assert_eq!(grow_capacity(4, 5), 8);
    assert_eq!(grow_capacity(4, 20), 20);
}

#[test]
fn capacity_sequence_doubles() {
    let mut da: DArray<u8> = DArray::new();
    let mut caps = Vec::new();
    for i in 0..5 {
        da.push_back(i);
        caps.push(da.capacity());
    }
    assert_eq!(caps, [1, 2, 4, 4, 8]);
}

#[test]
fn reserve_never_shrinks() {
    let mut da: DArray<u8> = DArray::new();
    da.reserve(0);
    assert_eq!(da.capacity(), 0);
    da.reserve(10);
    assert_eq!(da.capacity(), 10);
    da.reserve(5);
    assert_eq!(da.capacity(), 10);
}

#[test]
fn resize_zero_fills() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[7, 8]);
    da.resize(5);
    assert_eq!(da.as_slice(), [7, 8, 0, 0, 0]);
    da.resize(0);
    assert_eq!(da.len(), 0);
    assert!(da.capacity() >= 5);
}

#[test]
fn push_back_empty_array_returns_len() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[1, 2, 3]);
    assert_eq!(da.push_back_array(&[]), 3);
    assert_eq!(da.len(), 3);
    assert_eq!(da.push_back_array(&[4]), 3);
}

#[test]
fn clear_keeps_capacity() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[1, 2, 3]);
    let cap = da.capacity();
    da.clear();
    assert_eq!(da.len(), 0);
    assert_eq!(da.capacity(), cap);
}

#[test]
fn find_missing_is_invalid() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[1, 2, 1]);
    assert_eq!(da.find(&9, 0), DARRAY_INVALID);
    assert_eq!(da.find(&1, 0), 0);
    assert_eq!(da.find(&1, 1), 2);
}

#[test]
fn erase_and_pop_back() {
    let mut da: DArray<u8> = DArray::new();
    da.push_back_array(&[1, 2, 3, 4]);
    da.erase(1);
    assert_eq!(da.as_slice(), [1, 3, 4]);
    da.pop_back();
    assert_eq!(da.as_slice(), [1, 3]);
    da.erase(0);
    da.erase(0);
    assert_eq!(da.len(), 0);
}
