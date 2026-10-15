#include "preflight-gpu-sampler.hpp"

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

Sample ParseIokitPerformanceStatistics(const std::vector<std::pair<std::string, double>> &)
{
	return {};
}

Sample ParsePdhGpuEngineCounters(std::string_view)
{
	return {};
}

std::optional<double> ParseAmdGpuBusyPercent(std::string_view)
{
	return std::nullopt;
}

std::optional<double> AverageNvmlGpuUtilization(const std::vector<double> &)
{
	return std::nullopt;
}

std::optional<double> AverageNvmlVideoEncodeUtilization(const std::vector<double> &)
{
	return std::nullopt;
}

void WindowAverager::Add(const Sample &)
{
}

Sample WindowAverager::TakeAverage()
{
	return {};
}

} // namespace preflight::gpu
