//! `util/profiler-snapshot.c`: the snapshot accessors and free.
use core::ffi::{c_char, c_void};

/// Independent declaration of `DARRAY(T)`.
#[repr(C)]
#[derive(Debug)]
pub struct OracleDarray<T> {
    pub array: *mut T,
    pub num: usize,
    pub capacity: usize,
}

/// Independent declaration of `struct profiler_time_entry`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct OracleTimeEntry {
    pub time_delta: u64,
    pub count: u64,
}

/// Independent declaration of `struct profiler_snapshot_entry`.
#[repr(C)]
#[derive(Debug)]
pub struct OracleSnapshotEntry {
    pub name: *const c_char,
    pub times: OracleDarray<OracleTimeEntry>,
    pub min_time: u64,
    pub max_time: u64,
    pub overall_count: u64,
    pub times_between_calls: OracleDarray<OracleTimeEntry>,
    pub expected_time_between_calls: u64,
    pub min_time_between_calls: u64,
    pub max_time_between_calls: u64,
    pub overall_between_calls_count: u64,
    pub children: OracleDarray<OracleSnapshotEntry>,
}

/// Independent declaration of `struct profiler_snapshot`.
#[repr(C)]
#[derive(Debug)]
pub struct OracleSnapshot {
    pub roots: OracleDarray<OracleSnapshotEntry>,
}

pub type OracleEnumFunc =
    Option<unsafe extern "C" fn(context: *mut c_void, entry: *mut OracleSnapshotEntry) -> bool>;
pub type OracleFilterFunc =
    Option<unsafe extern "C" fn(data: *mut c_void, name: *const c_char, remove: *mut bool) -> bool>;

unsafe extern "C" {
    pub fn oracle_profile_snapshot_free(snap: *mut OracleSnapshot);
    pub fn oracle_profiler_snapshot_num_roots(snap: *mut OracleSnapshot) -> usize;
    pub fn oracle_profiler_snapshot_enumerate_roots(
        snap: *mut OracleSnapshot,
        func: OracleEnumFunc,
        context: *mut c_void,
    );
    pub fn oracle_profiler_snapshot_filter_roots(
        snap: *mut OracleSnapshot,
        func: OracleFilterFunc,
        data: *mut c_void,
    );
    pub fn oracle_profiler_snapshot_num_children(entry: *mut OracleSnapshotEntry) -> usize;
    pub fn oracle_profiler_snapshot_enumerate_children(
        entry: *mut OracleSnapshotEntry,
        func: OracleEnumFunc,
        context: *mut c_void,
    );
    pub fn oracle_profiler_snapshot_entry_name(entry: *mut OracleSnapshotEntry) -> *const c_char;
    pub fn oracle_profiler_snapshot_entry_times(
        entry: *mut OracleSnapshotEntry,
    ) -> *mut OracleDarray<OracleTimeEntry>;
    pub fn oracle_profiler_snapshot_entry_overall_count(entry: *mut OracleSnapshotEntry) -> u64;
    pub fn oracle_profiler_snapshot_entry_min_time(entry: *mut OracleSnapshotEntry) -> u64;
    pub fn oracle_profiler_snapshot_entry_max_time(entry: *mut OracleSnapshotEntry) -> u64;
    pub fn oracle_profiler_snapshot_entry_times_between_calls(
        entry: *mut OracleSnapshotEntry,
    ) -> *mut OracleDarray<OracleTimeEntry>;
    pub fn oracle_profiler_snapshot_entry_expected_time_between_calls(
        entry: *mut OracleSnapshotEntry,
    ) -> u64;
    pub fn oracle_profiler_snapshot_entry_min_time_between_calls(
        entry: *mut OracleSnapshotEntry,
    ) -> u64;
    pub fn oracle_profiler_snapshot_entry_max_time_between_calls(
        entry: *mut OracleSnapshotEntry,
    ) -> u64;
    pub fn oracle_profiler_snapshot_entry_overall_between_calls_count(
        entry: *mut OracleSnapshotEntry,
    ) -> u64;

    pub fn oracle_profiler_snapshot_size() -> usize;
    pub fn oracle_profiler_snapshot_align() -> usize;
    pub fn oracle_profiler_snapshot_offset_roots() -> usize;
    pub fn oracle_profiler_snapshot_entry_size() -> usize;
    pub fn oracle_profiler_snapshot_entry_align() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_name() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_times() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_min_time() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_max_time() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_overall_count() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_times_between_calls() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_expected_time_between_calls() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_min_time_between_calls() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_max_time_between_calls() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_overall_between_calls_count() -> usize;
    pub fn oracle_profiler_snapshot_entry_offset_children() -> usize;
    pub fn oracle_profiler_time_entry_size() -> usize;
    pub fn oracle_profiler_time_entry_align() -> usize;
    pub fn oracle_profiler_time_entry_offset_time_delta() -> usize;
    pub fn oracle_profiler_time_entry_offset_count() -> usize;
}
