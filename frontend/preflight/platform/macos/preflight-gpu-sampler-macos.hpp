#pragma once

#include "../../preflight-gpu-sampler.hpp"

#include <CoreFoundation/CoreFoundation.h>

namespace preflight::gpu {

Sample ParseMacPerformanceStatistics(CFDictionaryRef statistics);

} // namespace preflight::gpu
