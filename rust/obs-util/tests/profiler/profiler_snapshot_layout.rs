//! Tier 2: the `#[repr(C)]` mirrors of the private snapshot structs in
//! `libobs/util/profiler-snapshot.h` (and `struct profiler_time_entry` from
//! `profiler.h`) match the layout the C compiler produces. `profiler.c`
//! builds snapshots in C and the Rust shims read them in place, so this
//! layout is the contract.

use core::mem::{align_of, offset_of, size_of};

use obs_c_oracle::profiler_snapshot as c;
use obs_util::ffi::profiler_snapshot::{
    profiler_snapshot, profiler_snapshot_entry, profiler_time_entry,
};
use proptest as _;

#[test]
fn profiler_snapshot_layout_matches_c_header() {
    // SAFETY: the oracle layout functions take no arguments and only return constants.
    unsafe {
        assert_eq!(
            size_of::<profiler_snapshot>(),
            c::oracle_profiler_snapshot_size()
        );
        assert_eq!(
            align_of::<profiler_snapshot>(),
            c::oracle_profiler_snapshot_align()
        );
        assert_eq!(
            offset_of!(profiler_snapshot, roots),
            c::oracle_profiler_snapshot_offset_roots()
        );
    }
}

#[test]
fn profiler_snapshot_entry_layout_matches_c_header() {
    type E = profiler_snapshot_entry;
    // SAFETY: as above.
    unsafe {
        assert_eq!(size_of::<E>(), c::oracle_profiler_snapshot_entry_size());
        assert_eq!(align_of::<E>(), c::oracle_profiler_snapshot_entry_align());
        assert_eq!(
            offset_of!(E, name),
            c::oracle_profiler_snapshot_entry_offset_name()
        );
        assert_eq!(
            offset_of!(E, times),
            c::oracle_profiler_snapshot_entry_offset_times()
        );
        assert_eq!(
            offset_of!(E, min_time),
            c::oracle_profiler_snapshot_entry_offset_min_time()
        );
        assert_eq!(
            offset_of!(E, max_time),
            c::oracle_profiler_snapshot_entry_offset_max_time()
        );
        assert_eq!(
            offset_of!(E, overall_count),
            c::oracle_profiler_snapshot_entry_offset_overall_count()
        );
        assert_eq!(
            offset_of!(E, times_between_calls),
            c::oracle_profiler_snapshot_entry_offset_times_between_calls()
        );
        assert_eq!(
            offset_of!(E, expected_time_between_calls),
            c::oracle_profiler_snapshot_entry_offset_expected_time_between_calls()
        );
        assert_eq!(
            offset_of!(E, min_time_between_calls),
            c::oracle_profiler_snapshot_entry_offset_min_time_between_calls()
        );
        assert_eq!(
            offset_of!(E, max_time_between_calls),
            c::oracle_profiler_snapshot_entry_offset_max_time_between_calls()
        );
        assert_eq!(
            offset_of!(E, overall_between_calls_count),
            c::oracle_profiler_snapshot_entry_offset_overall_between_calls_count()
        );
        assert_eq!(
            offset_of!(E, children),
            c::oracle_profiler_snapshot_entry_offset_children()
        );
    }
}

#[test]
fn profiler_time_entry_layout_matches_c_header() {
    // SAFETY: as above.
    unsafe {
        assert_eq!(
            size_of::<profiler_time_entry>(),
            c::oracle_profiler_time_entry_size()
        );
        assert_eq!(
            align_of::<profiler_time_entry>(),
            c::oracle_profiler_time_entry_align()
        );
        assert_eq!(
            offset_of!(profiler_time_entry, time_delta),
            c::oracle_profiler_time_entry_offset_time_delta()
        );
        assert_eq!(
            offset_of!(profiler_time_entry, count),
            c::oracle_profiler_time_entry_offset_count()
        );
    }
}
