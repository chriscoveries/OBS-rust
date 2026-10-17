//! Tier 1: the safe enumeration and filter loops behind the snapshot
//! accessors, on a plain `Vec`.

use obs_c_oracle as _;
use obs_util::profiler_snapshot::{FilterStep, enumerate, filter};
use proptest as _;

#[test]
fn enumerate_visits_in_order() {
    let mut v = vec![1, 2, 3];
    let mut seen = Vec::new();
    enumerate(&mut v, |x| {
        seen.push(*x);
        true
    });
    assert_eq!(seen, [1, 2, 3]);
}

#[test]
fn enumerate_stops_when_the_callback_returns_false() {
    let mut v = vec![1, 2, 3];
    let mut seen = Vec::new();
    enumerate(&mut v, |x| {
        seen.push(*x);
        *x != 2
    });
    assert_eq!(seen, [1, 2]);
}

#[test]
fn enumerate_empty_never_calls() {
    let mut v: Vec<i32> = Vec::new();
    enumerate(&mut v, |_| panic!("called"));
}

#[test]
fn filter_removes_marked_entries_and_sees_every_entry_once() {
    let mut v = vec![1, 2, 3, 4, 5];
    let mut seen = Vec::new();
    filter(&mut v, |x| {
        seen.push(*x);
        FilterStep {
            keep_going: true,
            remove: *x % 2 == 0,
        }
    });
    assert_eq!(seen, [1, 2, 3, 4, 5]);
    assert_eq!(v, [1, 3, 5]);
}

/// The entry the callback stops on is still removed if it asked to be.
#[test]
fn filter_removes_before_stopping() {
    let mut v = vec![1, 2, 3];
    let mut seen = Vec::new();
    filter(&mut v, |x| {
        seen.push(*x);
        FilterStep {
            keep_going: *x != 2,
            remove: *x == 2,
        }
    });
    assert_eq!(seen, [1, 2]);
    assert_eq!(v, [1, 3]);
}

#[test]
fn filter_can_remove_consecutive_entries() {
    let mut v = vec![1, 2, 3, 4];
    filter(&mut v, |x| FilterStep {
        keep_going: true,
        remove: *x < 4,
    });
    assert_eq!(v, [4]);
}
