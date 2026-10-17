#include "build/libobs-util.pch.h"

static void free_snapshot_entry(profiler_snapshot_entry_t *entry)
{
	for (size_t i = 0; i < entry->children.num; i++)
		free_snapshot_entry(&entry->children.array[i]);

	da_free(entry->children);
	da_free(entry->times_between_calls);
	da_free(entry->times);
}

void profile_snapshot_free(profiler_snapshot_t *snap)
{
	if (!snap)
		return;

	for (size_t i = 0; i < snap->roots.num; i++)
		free_snapshot_entry(&snap->roots.array[i]);

	da_free(snap->roots);
	bfree(snap);
}

size_t profiler_snapshot_num_roots(profiler_snapshot_t *snap)
{
	return snap ? snap->roots.num : 0;
}

void profiler_snapshot_enumerate_roots(profiler_snapshot_t *snap, profiler_entry_enum_func func, void *context)
{
	if (!snap)
		return;

	for (size_t i = 0; i < snap->roots.num; i++)
		if (!func(context, &snap->roots.array[i]))
			break;
}

void profiler_snapshot_filter_roots(profiler_snapshot_t *snap, profiler_name_filter_func func, void *data)
{
	for (size_t i = 0; i < snap->roots.num;) {
		bool remove = false;
		bool res = func(data, snap->roots.array[i].name, &remove);

		if (remove) {
			free_snapshot_entry(&snap->roots.array[i]);
			da_erase(snap->roots, i);
		}

		if (!res)
			break;

		if (!remove)
			i += 1;
	}
}

size_t profiler_snapshot_num_children(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->children.num : 0;
}

void profiler_snapshot_enumerate_children(profiler_snapshot_entry_t *entry, profiler_entry_enum_func func,
					  void *context)
{
	if (!entry)
		return;

	for (size_t i = 0; i < entry->children.num; i++)
		if (!func(context, &entry->children.array[i]))
			break;
}

const char *profiler_snapshot_entry_name(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->name : NULL;
}

profiler_time_entries_t *profiler_snapshot_entry_times(profiler_snapshot_entry_t *entry)
{
	return entry ? &entry->times : NULL;
}

uint64_t profiler_snapshot_entry_overall_count(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->overall_count : 0;
}

uint64_t profiler_snapshot_entry_min_time(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->min_time : 0;
}

uint64_t profiler_snapshot_entry_max_time(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->max_time : 0;
}

profiler_time_entries_t *profiler_snapshot_entry_times_between_calls(profiler_snapshot_entry_t *entry)
{
	return entry ? &entry->times_between_calls : NULL;
}

uint64_t profiler_snapshot_entry_expected_time_between_calls(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->expected_time_between_calls : 0;
}

uint64_t profiler_snapshot_entry_min_time_between_calls(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->min_time_between_calls : 0;
}

uint64_t profiler_snapshot_entry_max_time_between_calls(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->max_time_between_calls : 0;
}

uint64_t profiler_snapshot_entry_overall_between_calls_count(profiler_snapshot_entry_t *entry)
{
	return entry ? entry->overall_between_calls_count : 0;
}
