#include "preflight-gpu-sampler.hpp"

#include <cctype>
#include <cmath>
#include <sstream>
#include <memory>

namespace preflight::gpu {
namespace {

class UnavailableSampler final : public Sampler {
public:
	Sample Read() override { return {}; }
};

} // namespace

std::unique_ptr<Sampler> CreateUnavailableSampler()
{
	return std::make_unique<UnavailableSampler>();
}

Sample ParseIokitPerformanceStatistics(const std::vector<std::pair<std::string, double>> &values)
{
	Sample sample;
	for (const auto &[key, value] : values) {
		if (!std::isfinite(value) || value < 0.0 || value > 100.0) {
			continue;
		}
		if (key == "Device Utilization %") {
			sample.gpu_percent = value;
		} else if (key == "Renderer Utilization %") {
			sample.renderer_percent = value;
		} else if (key == "Tiler Utilization %") {
			sample.tiler_percent = value;
		}
	}
	return sample;
}

Sample ParsePdhGpuEngineCounters(std::string_view counters)
{
	Sample sample;
	double three_d_total = 0.0;
	double video_encode_total = 0.0;
	bool has_three_d = false;
	bool has_video_encode = false;
	std::istringstream lines{std::string(counters)};
	std::string line;
	while (std::getline(lines, line)) {
		const size_t separator = line.rfind('=');
		if (separator == std::string::npos) {
			continue;
		}
		const std::string instance = line.substr(0, separator);
		try {
			const double value = std::stod(line.substr(separator + 1));
			if (!std::isfinite(value) || value < 0.0 || value > 100.0) {
				continue;
			}
			if (instance.find("engtype_3D") != std::string::npos) {
				three_d_total += value;
				has_three_d = true;
			} else if (instance.find("engtype_VideoEncode") != std::string::npos) {
				video_encode_total += value;
				has_video_encode = true;
			}
		} catch (...) {
			// A stale or malformed PDH instance is simply unavailable for this sample.
		}
	}
	if (has_three_d) {
		sample.gpu_percent = three_d_total;
	}
	if (has_video_encode) {
		sample.video_encode_percent = video_encode_total;
	}
	return sample;
}

std::optional<double> ParseAmdGpuBusyPercent(std::string_view text)
{
	try {
		size_t consumed = 0;
		const double value = std::stod(std::string(text), &consumed);
		while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed]))) {
			++consumed;
		}
		if (consumed != text.size() || !std::isfinite(value) || value < 0.0 || value > 100.0) {
			return std::nullopt;
		}
		return value;
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<double> AverageNvmlGpuUtilization(const std::vector<double> &device_percentages)
{
	return AveragePercentages(device_percentages);
}

std::optional<double> AverageNvmlVideoEncodeUtilization(const std::vector<double> &device_percentages)
{
	return AveragePercentages(device_percentages);
}

std::optional<double> AveragePercentages(const std::vector<double> &percentages)
{
	double sum = 0.0;
	size_t count = 0;
	for (const double value : percentages) {
		if (std::isfinite(value) && value >= 0.0 && value <= 100.0) {
			sum += value;
			++count;
		}
	}
	if (count == 0) {
		return std::nullopt;
	}
	return sum / static_cast<double>(count);
}

void WindowAverager::Add(const Sample &sample)
{
	auto add = [](const std::optional<double> &value, double &sum, size_t &count) {
		if (value && std::isfinite(*value)) {
			sum += *value;
			++count;
		}
	};
	add(sample.gpu_percent, gpu_sum, gpu_count);
	add(sample.renderer_percent, renderer_sum, renderer_count);
	add(sample.tiler_percent, tiler_sum, tiler_count);
	add(sample.video_encode_percent, video_encode_sum, video_encode_count);
}

Sample WindowAverager::TakeAverage()
{
	auto average = [](double sum, size_t count) -> std::optional<double> {
		return count == 0 ? std::nullopt : std::optional<double>(sum / static_cast<double>(count));
	};
	Sample result{average(gpu_sum, gpu_count), average(renderer_sum, renderer_count),
		      average(tiler_sum, tiler_count), average(video_encode_sum, video_encode_count)};
	gpu_sum = renderer_sum = tiler_sum = video_encode_sum = 0.0;
	gpu_count = renderer_count = tiler_count = video_encode_count = 0;
	return result;
}

} // namespace preflight::gpu
