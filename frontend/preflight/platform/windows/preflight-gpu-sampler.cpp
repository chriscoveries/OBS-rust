#include "../../preflight-gpu-sampler.hpp"

namespace preflight::gpu {

std::unique_ptr<Sampler> CreatePlatformSampler()
{
	return CreateUnavailableSampler();
}

} // namespace preflight::gpu
