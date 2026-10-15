#include "preflight-bench-data.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace preflight::bench {
namespace {

/* Keep enough recent entries for the one-screen impact list without unbounded growth. */
constexpr size_t MAX_IMPACT_ENTRIES = 32;

} // namespace

MetricDelta CalculateImpactDelta(const obs_preflight_window &baseline, const obs_preflight_window &after)
{
	MetricDelta delta;
	delta.cpu_percentage_points = after.cpu_percent - baseline.cpu_percent;
	delta.render_time_ms = (static_cast<double>(after.average_frame_time_ns) -
				static_cast<double>(baseline.average_frame_time_ns)) /
			       1'000'000.0;
	delta.encoder_skipped_percentage_points = after.encoder_skip_percent - baseline.encoder_skip_percent;
	delta.missed_percentage_points = after.render_lag_percent - baseline.render_lag_percent;
	delta.output_dropped_percentage_points = after.output_drop_percent - baseline.output_drop_percent;
	delta.bitrate_kbps = after.output_bitrate_kbps - baseline.output_bitrate_kbps;
	delta.verdict_before = baseline.verdict;
	delta.verdict_after = after.verdict;
	return delta;
}

void ImpactTracker::RecordChanges(const std::vector<SettingChange> &changes)
{
	for (const SettingChange &change : changes) {
		ImpactEntry entry;
		entry.id = next_id++;
		entry.description = change.description;
		entry.key = change.key;
		entry.old_value = change.old_value;
		entry.new_value = change.new_value;
		if (latest_complete_window) {
			entry.baseline = latest_complete_window;
			entry.state = ImpactState::Settling;
		} else {
			entry.state = ImpactState::WaitingForBaseline;
		}
		entries.push_back(std::move(entry));
	}
	if (entries.size() > MAX_IMPACT_ENTRIES) {
		entries.erase(entries.begin(),
			      entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - MAX_IMPACT_ENTRIES));
	}
}

void ImpactTracker::ObserveCompleteWindow(const MetricWindow &window)
{
	if (latest_complete_window && window.sequence <= latest_complete_window->sequence) {
		return;
	}
	latest_complete_window = window;
	for (ImpactEntry &entry : entries) {
		if (entry.state == ImpactState::WaitingForBaseline) {
			/* This window followed the edit, so it cannot truthfully serve as its baseline. */
			entry.after = window;
			entry.state = ImpactState::NoBaseline;
		} else if (entry.state == ImpactState::Settling && entry.baseline &&
			   window.sequence > entry.baseline->sequence) {
			entry.after = window;
			entry.deltas = CalculateImpactDelta(entry.baseline->value, window.value);
			entry.state = ImpactState::Complete;
		}
	}
}

} // namespace preflight::bench
