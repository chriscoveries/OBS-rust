/* Test-only oracle: the original libobs/util/profiler-snapshot.c, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the Rust
 * implementation, plus the snapshot struct layouts as the C compiler sees
 * them. */
#include <stddef.h>

#define profile_snapshot_free oracle_profile_snapshot_free
#define profiler_snapshot_num_roots oracle_profiler_snapshot_num_roots
#define profiler_snapshot_enumerate_roots oracle_profiler_snapshot_enumerate_roots
#define profiler_snapshot_filter_roots oracle_profiler_snapshot_filter_roots
#define profiler_snapshot_num_children oracle_profiler_snapshot_num_children
#define profiler_snapshot_enumerate_children oracle_profiler_snapshot_enumerate_children
#define profiler_snapshot_entry_name oracle_profiler_snapshot_entry_name
#define profiler_snapshot_entry_times oracle_profiler_snapshot_entry_times
#define profiler_snapshot_entry_overall_count oracle_profiler_snapshot_entry_overall_count
#define profiler_snapshot_entry_min_time oracle_profiler_snapshot_entry_min_time
#define profiler_snapshot_entry_max_time oracle_profiler_snapshot_entry_max_time
#define profiler_snapshot_entry_times_between_calls oracle_profiler_snapshot_entry_times_between_calls
#define profiler_snapshot_entry_expected_time_between_calls oracle_profiler_snapshot_entry_expected_time_between_calls
#define profiler_snapshot_entry_min_time_between_calls oracle_profiler_snapshot_entry_min_time_between_calls
#define profiler_snapshot_entry_max_time_between_calls oracle_profiler_snapshot_entry_max_time_between_calls
#define profiler_snapshot_entry_overall_between_calls_count oracle_profiler_snapshot_entry_overall_between_calls_count

#include "util/profiler-snapshot.c.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_profiler_snapshot_size(void);
size_t oracle_profiler_snapshot_align(void);
size_t oracle_profiler_snapshot_offset_roots(void);
size_t oracle_profiler_snapshot_entry_size(void);
size_t oracle_profiler_snapshot_entry_align(void);
size_t oracle_profiler_snapshot_entry_offset_name(void);
size_t oracle_profiler_snapshot_entry_offset_times(void);
size_t oracle_profiler_snapshot_entry_offset_min_time(void);
size_t oracle_profiler_snapshot_entry_offset_max_time(void);
size_t oracle_profiler_snapshot_entry_offset_overall_count(void);
size_t oracle_profiler_snapshot_entry_offset_times_between_calls(void);
size_t oracle_profiler_snapshot_entry_offset_expected_time_between_calls(void);
size_t oracle_profiler_snapshot_entry_offset_min_time_between_calls(void);
size_t oracle_profiler_snapshot_entry_offset_max_time_between_calls(void);
size_t oracle_profiler_snapshot_entry_offset_overall_between_calls_count(void);
size_t oracle_profiler_snapshot_entry_offset_children(void);
size_t oracle_profiler_time_entry_size(void);
size_t oracle_profiler_time_entry_align(void);
size_t oracle_profiler_time_entry_offset_time_delta(void);
size_t oracle_profiler_time_entry_offset_count(void);

size_t oracle_profiler_snapshot_size(void)
{
	return sizeof(struct profiler_snapshot);
}
size_t oracle_profiler_snapshot_align(void)
{
	return _Alignof(struct profiler_snapshot);
}
size_t oracle_profiler_snapshot_offset_roots(void)
{
	return offsetof(struct profiler_snapshot, roots);
}
size_t oracle_profiler_snapshot_entry_size(void)
{
	return sizeof(struct profiler_snapshot_entry);
}
size_t oracle_profiler_snapshot_entry_align(void)
{
	return _Alignof(struct profiler_snapshot_entry);
}
size_t oracle_profiler_snapshot_entry_offset_name(void)
{
	return offsetof(struct profiler_snapshot_entry, name);
}
size_t oracle_profiler_snapshot_entry_offset_times(void)
{
	return offsetof(struct profiler_snapshot_entry, times);
}
size_t oracle_profiler_snapshot_entry_offset_min_time(void)
{
	return offsetof(struct profiler_snapshot_entry, min_time);
}
size_t oracle_profiler_snapshot_entry_offset_max_time(void)
{
	return offsetof(struct profiler_snapshot_entry, max_time);
}
size_t oracle_profiler_snapshot_entry_offset_overall_count(void)
{
	return offsetof(struct profiler_snapshot_entry, overall_count);
}
size_t oracle_profiler_snapshot_entry_offset_times_between_calls(void)
{
	return offsetof(struct profiler_snapshot_entry, times_between_calls);
}
size_t oracle_profiler_snapshot_entry_offset_expected_time_between_calls(void)
{
	return offsetof(struct profiler_snapshot_entry, expected_time_between_calls);
}
size_t oracle_profiler_snapshot_entry_offset_min_time_between_calls(void)
{
	return offsetof(struct profiler_snapshot_entry, min_time_between_calls);
}
size_t oracle_profiler_snapshot_entry_offset_max_time_between_calls(void)
{
	return offsetof(struct profiler_snapshot_entry, max_time_between_calls);
}
size_t oracle_profiler_snapshot_entry_offset_overall_between_calls_count(void)
{
	return offsetof(struct profiler_snapshot_entry, overall_between_calls_count);
}
size_t oracle_profiler_snapshot_entry_offset_children(void)
{
	return offsetof(struct profiler_snapshot_entry, children);
}
size_t oracle_profiler_time_entry_size(void)
{
	return sizeof(struct profiler_time_entry);
}
size_t oracle_profiler_time_entry_align(void)
{
	return _Alignof(struct profiler_time_entry);
}
size_t oracle_profiler_time_entry_offset_time_delta(void)
{
	return offsetof(struct profiler_time_entry, time_delta);
}
size_t oracle_profiler_time_entry_offset_count(void)
{
	return offsetof(struct profiler_time_entry, count);
}
