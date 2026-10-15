#include "preflight-gpu-sampler-macos.hpp"

namespace preflight::gpu {

Sample ParseMacPerformanceStatistics(CFDictionaryRef)
{
	return {};
}

std::unique_ptr<Sampler> CreatePlatformSampler()
{
	return CreateUnavailableSampler();
}

} // namespace preflight::gpu
