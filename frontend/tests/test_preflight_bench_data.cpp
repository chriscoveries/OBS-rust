#include "../preflight/preflight-bench-data.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL test_preflight_bench_data: " << message << '\n';
		std::exit(1);
	}
}

obs_preflight_window window(uint64_t end_ms, double cpu, double render_ns, double skipped, double missed,
			    double dropped, double bitrate, int32_t verdict)
{
	obs_preflight_window result{};
	result.end_ms = end_ms;
	result.cpu_percent = cpu;
	result.encoder_frames = 100;
	result.average_frame_time_ns = render_ns;
	result.encoder_skip_percent = skipped;
	result.render_lag_percent = missed;
	result.output_drop_percent = dropped;
	result.output_bitrate_kbps = bitrate;
	result.verdict = verdict;
	return result;
}

void test_delta_from_recorded_windows()
{
	const auto baseline = window(500, 31.25, 10'000'000, 0.5, 0.25, 0.1, 4500, OBS_PREFLIGHT_PASS);
	const auto after = window(1000, 40.0, 11'500'000, 2.0, 1.75, 0.7, 4000, OBS_PREFLIGHT_MARGINAL);
	const preflight::bench::MetricWindow baseline_metrics{1, baseline, 32.0, 18.0, 10.0, 4.0};
	const preflight::bench::MetricWindow after_metrics{2, after, 44.5, 27.0, 12.0, 9.5};
	const auto delta = preflight::bench::CalculateImpactDelta(baseline_metrics, after_metrics);
	require(std::abs(delta.cpu_percentage_points - 8.75) < 0.000001,
		"CPU delta should be measured in percentage points");
	require(std::abs(delta.render_time_ms - 1.5) < 0.000001, "render delta should be reported in milliseconds");
	require(std::abs(delta.encoder_skipped_percentage_points - 1.5) < 0.000001,
		"encoder skip delta should be measured in percentage points");
	require(std::abs(delta.missed_percentage_points - 1.5) < 0.000001,
		"missed frame delta should be measured in percentage points");
	require(std::abs(delta.output_dropped_percentage_points - 0.6) < 0.000001,
		"dropped frame delta should be measured in percentage points");
	require(std::abs(delta.bitrate_kbps + 500.0) < 0.000001, "bitrate delta should be after minus baseline");
	require(delta.gpu_percentage_points && std::abs(*delta.gpu_percentage_points - 12.5) < 0.000001,
		"GPU delta should be optional percentage points");
	require(delta.gpu_video_encode_percentage_points &&
			std::abs(*delta.gpu_video_encode_percentage_points - 5.5) < 0.000001,
		"GPU VideoEncode delta should be optional percentage points");
	require(delta.verdict_before == OBS_PREFLIGHT_PASS && delta.verdict_after == OBS_PREFLIGHT_MARGINAL,
		"impact should retain both verdicts");
}

void test_gpu_delta_is_unavailable_without_both_windows()
{
	const preflight::bench::MetricWindow baseline{1, window(500, 30, 8'000'000, 0, 0, 0, 5000, OBS_PREFLIGHT_PASS),
						      22.0};
	const preflight::bench::MetricWindow after{2, window(1000, 30, 8'000'000, 0, 0, 0, 5000, OBS_PREFLIGHT_PASS),
						   std::nullopt};
	const auto delta = preflight::bench::CalculateImpactDelta(baseline, after);
	require(!delta.gpu_percentage_points, "an unavailable GPU sample must not create a zero-valued impact delta");
}

void test_verdict_reason_lists_the_thresholds_that_fired()
{
	using preflight::bench::BuildVerdictReason;
	auto result = window(1000, 35.0, 14'100'000.0, 0, 0, 3.33, 4500, OBS_PREFLIGHT_MARGINAL);
	result.frame_interval_ns = 16'666'667.0;
	result.congestion_percent = 35.0;
	require(BuildVerdictReason(result) ==
			"Render 14.1 / 16.67 ms > 75% · Dropped 3.33% > 1% · Congestion 35.0% > 33.3%",
		"verdict reason should use measured values and exact OBS warning rules");
	auto stale_encoder = window(1000, 35.0, 0.0, 0, 0, 0, 0, OBS_PREFLIGHT_FAIL);
	stale_encoder.encoder_frames = 0;
	require(BuildVerdictReason(stale_encoder) == "Encoder: 0 frames in window",
		"stale encoder reason should come from the engine window's frame count");
	auto clean = window(1000, 35.0, 8'000'000.0, 0, 0, 0, 4500, OBS_PREFLIGHT_PASS);
	require(BuildVerdictReason(clean) == "All OBS limits met", "passing reason should be deterministic");
}

void test_impact_waits_for_a_complete_after_window()
{
	using namespace preflight::bench;
	ImpactTracker tracker;
	tracker.ObserveCompleteWindow({10, window(500, 30, 8'000'000, 0, 0, 0, 5000, OBS_PREFLIGHT_PASS)});
	tracker.RecordChanges({{"Stream preset changed", "preset", "fast", "medium"}});
	require(tracker.Entries().size() == 1 && tracker.Entries().front().state == ImpactState::Settling &&
			tracker.Entries().front().baseline && tracker.Entries().front().baseline->sequence == 10 &&
			!tracker.Entries().front().after,
		"a setting change must retain the preceding complete baseline and stay settling");
	tracker.ObserveCompleteWindow({11, window(1000, 33, 9'000'000, 1, 0.5, 0, 4700, OBS_PREFLIGHT_PASS)});
	const auto &entry = tracker.Entries().front();
	require(entry.state == ImpactState::Complete && entry.after && entry.after->sequence == 11 && entry.deltas,
		"the first complete post-change window should finish the impact entry");
}

void test_change_without_prior_complete_window_has_no_false_baseline()
{
	using namespace preflight::bench;
	ImpactTracker tracker;
	tracker.RecordChanges({{"Video FPS changed", "fps", "60", "30"}});
	require(tracker.Entries().front().state == ImpactState::WaitingForBaseline &&
			!tracker.Entries().front().baseline,
		"a change before the first complete window must not invent a baseline");
	tracker.ObserveCompleteWindow({1, window(500, 10, 5'000'000, 0, 0, 0, 1000, OBS_PREFLIGHT_PASS)});
	require(tracker.Entries().front().state == ImpactState::NoBaseline && tracker.Entries().front().after &&
			!tracker.Entries().front().deltas,
		"a post-change window must not be mislabeled as a pre-change baseline");
}

} // namespace

int main()
{
	test_delta_from_recorded_windows();
	test_gpu_delta_is_unavailable_without_both_windows();
	test_verdict_reason_lists_the_thresholds_that_fired();
	test_impact_waits_for_a_complete_after_window();
	test_change_without_prior_complete_window_has_no_false_baseline();
	std::cout << "PASS test_preflight_bench_data\n";
	return 0;
}
