#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace preflight::gpu {

struct Sample {
	std::optional<double> gpu_percent;
	std::optional<double> renderer_percent;
	std::optional<double> tiler_percent;
	std::optional<double> video_encode_percent;
};

class Sampler {
public:
	virtual ~Sampler() = default;
	virtual Sample Read() = 0;
};

/* A sample without a provider stays unavailable; it is never represented by zero. */
std::unique_ptr<Sampler> CreateUnavailableSampler();
std::unique_ptr<Sampler> CreatePlatformSampler();

/* Pure parsers make the provider formats testable without live hardware. */
Sample ParseIokitPerformanceStatistics(const std::vector<std::pair<std::string, double>> &values);
Sample ParsePdhGpuEngineCounters(std::string_view counters);
std::optional<double> ParseAmdGpuBusyPercent(std::string_view text);
std::optional<double> AverageNvmlGpuUtilization(const std::vector<double> &device_percentages);
std::optional<double> AverageNvmlVideoEncodeUtilization(const std::vector<double> &device_percentages);
std::optional<double> AveragePercentages(const std::vector<double> &percentages);

/* Averages only values that the provider actually reported. */
class WindowAverager {
	double gpu_sum = 0.0;
	double renderer_sum = 0.0;
	double tiler_sum = 0.0;
	double video_encode_sum = 0.0;
	size_t gpu_count = 0;
	size_t renderer_count = 0;
	size_t tiler_count = 0;
	size_t video_encode_count = 0;

public:
	void Add(const Sample &sample);
	Sample TakeAverage();
};

} // namespace preflight::gpu
