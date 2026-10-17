#include "platform/macos/preflight-gpu-sampler-macos.hpp"

#include <CoreFoundation/CoreFoundation.h>

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
		std::cerr << "FAIL test_preflight_gpu_sampler_macos: " << message << '\n';
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

void test_iokit_recorded_dictionary()
{
	const std::string xml = fixture("iokit-performance-statistics.plist");
	CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(xml.data()),
				      static_cast<CFIndex>(xml.size()));
	require(data != nullptr, "could not create recorded IOKit plist data");
	CFErrorRef error = nullptr;
	CFPropertyListRef properties =
		CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, &error);
	CFRelease(data);
	if (error) {
		CFRelease(error);
	}
	require(properties && CFGetTypeID(properties) == CFDictionaryGetTypeID(),
		"recorded IOKit fixture is not a dictionary");
	const auto sample = preflight::gpu::ParseMacPerformanceStatistics(static_cast<CFDictionaryRef>(properties));
	CFRelease(properties);
	require(sample.gpu_percent && *sample.gpu_percent == 37.0,
		"IOKit Device Utilization % was not parsed from the recorded fixture");
	require(sample.renderer_percent && *sample.renderer_percent == 21.0 && sample.tiler_percent &&
			*sample.tiler_percent == 14.0,
		"IOKit renderer or tiler utilization was not parsed");
	const auto missing = preflight::gpu::ParseMacPerformanceStatistics(nullptr);
	require(!missing.gpu_percent && !missing.renderer_percent && !missing.tiler_percent,
		"missing IOKit keys must remain unavailable");
}

} // namespace

int main()
{
	try {
		test_iokit_recorded_dictionary();
	} catch (const std::exception &error) {
		std::cerr << "FAIL test_preflight_gpu_sampler_macos: " << error.what() << '\n';
		return 1;
	}
	std::cout << "PASS test_preflight_gpu_sampler_macos\n";
	return 0;
}
