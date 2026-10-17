#include "preflight-gpu-sampler-macos.hpp"

#include <IOKit/IOKitLib.h>

#include <cmath>
#include <string>
#include <vector>

namespace preflight::gpu {
namespace {

std::optional<double> dictionary_number(CFDictionaryRef dictionary, CFStringRef key)
{
	if (!dictionary) {
		return std::nullopt;
	}
	const CFTypeRef value = CFDictionaryGetValue(dictionary, key);
	if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) {
		return std::nullopt;
	}
	double result = 0.0;
	if (!CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &result) ||
	    !std::isfinite(result) || result < 0.0 || result > 100.0) {
		return std::nullopt;
	}
	return result;
}

class MacSampler final : public Sampler {
public:
	Sample Read() override
	{
		io_iterator_t accelerators = IO_OBJECT_NULL;
		const kern_return_t status = IOServiceGetMatchingServices(
			kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &accelerators);
		if (status != KERN_SUCCESS) {
			return {};
		}

		WindowAverager devices;
		io_service_t accelerator = IOIteratorNext(accelerators);
		while (accelerator != IO_OBJECT_NULL) {
			CFMutableDictionaryRef properties = nullptr;
			if (IORegistryEntryCreateCFProperties(accelerator, &properties, kCFAllocatorDefault, 0) ==
				    KERN_SUCCESS &&
			    properties) {
				const CFTypeRef value =
					CFDictionaryGetValue(properties, CFSTR("PerformanceStatistics"));
				if (value && CFGetTypeID(value) == CFDictionaryGetTypeID()) {
					devices.Add(ParseMacPerformanceStatistics(static_cast<CFDictionaryRef>(value)));
				}
				CFRelease(properties);
			}
			IOObjectRelease(accelerator);
			accelerator = IOIteratorNext(accelerators);
		}
		IOObjectRelease(accelerators);
		return devices.TakeAverage();
	}
};

} // namespace

Sample ParseMacPerformanceStatistics(CFDictionaryRef statistics)
{
	std::vector<std::pair<std::string, double>> values;
	const std::pair<const char *, CFStringRef> keys[] = {
		{"Device Utilization %", CFSTR("Device Utilization %")},
		{"Renderer Utilization %", CFSTR("Renderer Utilization %")},
		{"Tiler Utilization %", CFSTR("Tiler Utilization %")},
	};
	for (const auto &[name, key] : keys) {
		if (const auto value = dictionary_number(statistics, key)) {
			values.emplace_back(name, *value);
		}
	}
	return ParseIokitPerformanceStatistics(values);
}

std::unique_ptr<Sampler> CreatePlatformSampler()
{
	return std::make_unique<MacSampler>();
}

} // namespace preflight::gpu
