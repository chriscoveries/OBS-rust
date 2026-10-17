#include "../preflight/preflight-gpu-sampler.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL test_preflight_gpu_sampler: " << message << '\n';
		std::exit(1);
	}
}

std::string fixture(const char *name)
{
	std::ifstream file(std::string(PREFLIGHT_GPU_FIXTURE_DIR) + "/" + name);
	if (!file) {
		throw std::runtime_error("could not open GPU fixture");
	}
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}

void test_provider_fixtures()
{
	const auto pdh = preflight::gpu::ParsePdhGpuEngineCounters(fixture("pdh-gpu-engine.txt"));
	require(pdh.gpu_percent && *pdh.gpu_percent == 25.75, "PDH parser did not sum 3D engine instances");
	require(pdh.video_encode_percent && *pdh.video_encode_percent == 11.0,
		"PDH parser did not retain VideoEncode utilization separately");
	const auto amd = preflight::gpu::ParseAmdGpuBusyPercent(fixture("amd-gpu-busy.txt"));
	require(amd && *amd == 42.0, "AMD sysfs GPU busy percentage was not parsed");

	/* A stub NVML device set replays readings recorded in the fixture. */
	std::istringstream nvml(fixture("nvml-utilization.txt"));
	std::string line;
	std::vector<double> gpu;
	std::vector<double> encode;
	while (std::getline(nvml, line)) {
		const auto separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		const double value = std::stod(line.substr(separator + 1));
		(line.substr(0, separator) == "gpu" ? gpu : encode).push_back(value);
	}
	const auto nvml_gpu = preflight::gpu::AverageNvmlGpuUtilization(gpu);
	const auto nvml_encode = preflight::gpu::AverageNvmlVideoEncodeUtilization(encode);
	require(nvml_gpu && *nvml_gpu == 39.0 && nvml_encode && *nvml_encode == 15.0,
		"NVML stub did not average device utilization readings");
}

void test_window_average_and_unavailable()
{
	preflight::gpu::WindowAverager average;
	average.Add({42.0, 20.0, 10.0, 5.0});
	average.Add({std::nullopt, 30.0, std::nullopt, std::nullopt});
	const auto result = average.TakeAverage();
	require(result.gpu_percent && *result.gpu_percent == 42.0 && result.renderer_percent &&
			*result.renderer_percent == 25.0 && result.tiler_percent && *result.tiler_percent == 10.0 &&
			result.video_encode_percent && *result.video_encode_percent == 5.0,
		"GPU window average treated missing samples as zero");
	const auto empty = average.TakeAverage();
	require(!empty.gpu_percent && !empty.renderer_percent && !empty.tiler_percent && !empty.video_encode_percent,
		"an empty GPU window must be unavailable");

	auto unavailable = preflight::gpu::CreateUnavailableSampler();
	const auto missing = unavailable->Read();
	require(!missing.gpu_percent && !missing.renderer_percent && !missing.tiler_percent &&
			!missing.video_encode_percent,
		"unavailable platform sampler must not report zero utilization");
}

} // namespace

int main()
{
	try {
		test_provider_fixtures();
		test_window_average_and_unavailable();
	} catch (const std::exception &error) {
		std::cerr << "FAIL test_preflight_gpu_sampler: " << error.what() << '\n';
		return 1;
	}
	std::cout << "PASS test_preflight_gpu_sampler\n";
	return 0;
}
