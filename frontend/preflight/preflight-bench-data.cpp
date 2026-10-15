#include "preflight-bench-data.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <utility>

namespace preflight::bench {
namespace {

/* Keep enough recent entries for the one-screen impact list without unbounded growth. */
constexpr size_t MAX_IMPACT_ENTRIES = 32;

} // namespace

MetricDelta CalculateImpactDelta(const MetricWindow &baseline, const MetricWindow &after)
{
	MetricDelta delta;
	delta.cpu_percentage_points = after.value.cpu_percent - baseline.value.cpu_percent;
	delta.render_time_ms = (after.value.average_frame_time_ns - baseline.value.average_frame_time_ns) / 1'000'000.0;
	delta.encoder_skipped_percentage_points =
		after.value.encoder_skip_percent - baseline.value.encoder_skip_percent;
	delta.missed_percentage_points = after.value.render_lag_percent - baseline.value.render_lag_percent;
	delta.output_dropped_percentage_points = after.value.output_drop_percent - baseline.value.output_drop_percent;
	delta.bitrate_kbps = after.value.output_bitrate_kbps - baseline.value.output_bitrate_kbps;
	if (baseline.gpu_percent && after.gpu_percent) {
		delta.gpu_percentage_points = *after.gpu_percent - *baseline.gpu_percent;
	}
	if (baseline.gpu_video_encode_percent && after.gpu_video_encode_percent) {
		delta.gpu_video_encode_percentage_points =
			*after.gpu_video_encode_percent - *baseline.gpu_video_encode_percent;
	}
	delta.verdict_before = baseline.value.verdict;
	delta.verdict_after = after.value.verdict;
	return delta;
}

std::string BuildVerdictReason(const obs_preflight_window &window)
{
	std::vector<std::string> reasons;
	if (window.encoder_frames == 0) {
		reasons.emplace_back("Encoder: 0 frames in window");
	}
	if (window.frame_interval_ns > 0.0) {
		const double render_fraction = window.average_frame_time_ns / window.frame_interval_ns;
		if (render_fraction > policy::OBS_RENDER_DANGER_FRACTION ||
		    render_fraction > policy::OBS_RENDER_WARNING_FRACTION) {
			std::ostringstream reason;
			reason << "Render " << std::fixed << std::setprecision(1)
			       << window.average_frame_time_ns / 1'000'000.0 << " / " << std::setprecision(2)
			       << window.frame_interval_ns / 1'000'000.0 << " ms > " << std::setprecision(0)
			       << (render_fraction > policy::OBS_RENDER_DANGER_FRACTION
					   ? 100.0
					   : policy::OBS_RENDER_WARNING_FRACTION * 100.0)
			       << '%';
			reasons.push_back(reason.str());
		}
	}
	auto add_percent_reason = [&reasons](const char *name, double value, double warning, double danger,
					     int value_precision, int threshold_precision) {
		if (value > danger || value > warning) {
			std::ostringstream reason;
			reason << name << ' ' << std::fixed << std::setprecision(value_precision) << value << "% > "
			       << std::setprecision(threshold_precision) << (value > danger ? danger : warning) << '%';
			reasons.push_back(reason.str());
		}
	};
	add_percent_reason("Missed", window.render_lag_percent, policy::OBS_STATS_WARNING_PERCENT,
			   policy::OBS_STATS_DANGER_PERCENT, 2, 0);
	if (window.encoder_skip_percent > policy::OBS_STATS_DANGER_PERCENT ||
	    window.encoder_skip_percent > policy::OBS_STATS_WARNING_PERCENT) {
		add_percent_reason("Skipped", window.encoder_skip_percent, policy::OBS_STATS_WARNING_PERCENT,
				   policy::OBS_STATS_DANGER_PERCENT, 2, 0);
	} else if (window.encoder_frames_skipped > policy::OBS_ENCODER_SKIP_STATUS_MIN_COUNT &&
		   window.encoder_skip_percent >= policy::OBS_ENCODER_SKIP_STATUS_PERCENT) {
		std::ostringstream reason;
		reason << "Skipped " << std::fixed << std::setprecision(2) << window.encoder_skip_percent
		       << "% >= " << policy::OBS_ENCODER_SKIP_STATUS_PERCENT << '%';
		reasons.push_back(reason.str());
	}
	add_percent_reason("Dropped", window.output_drop_percent, policy::OBS_STATS_WARNING_PERCENT,
			   policy::OBS_STATS_DANGER_PERCENT, 2, 0);
	add_percent_reason("Congestion", window.congestion_percent, policy::OBS_CONGESTION_WARNING_PERCENT,
			   policy::OBS_CONGESTION_DANGER_PERCENT, 1, 1);

	if (reasons.empty()) {
		if (window.verdict == OBS_PREFLIGHT_PASS) {
			return "All OBS limits met";
		}
		/* This fallback is only for future engine verdict conditions not represented in window fields. */
		return window.reason[0] ? std::string(window.reason) : "Preflight: reason unavailable";
	}
	std::ostringstream combined;
	for (size_t index = 0; index < reasons.size(); ++index) {
		if (index != 0) {
			combined << " · ";
		}
		combined << reasons[index];
	}
	return combined.str();
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
			entry.deltas = CalculateImpactDelta(*entry.baseline, window);
			entry.state = ImpactState::Complete;
		}
	}
}

} // namespace preflight::bench
