#include "../../preflight-gpu-sampler.hpp"

#include <dlfcn.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace preflight::gpu {
namespace {

using NvmlReturn = int;
using NvmlDevice = struct nvmlDevice_st *;

struct NvmlUtilization {
	unsigned int gpu;
	unsigned int memory;
};

class Nvml {
	void *library = nullptr;
	NvmlReturn (*shutdown)() = nullptr;
	NvmlReturn (*get_count)(unsigned int *) = nullptr;
	NvmlReturn (*get_device)(unsigned int, NvmlDevice *) = nullptr;
	NvmlReturn (*get_utilization)(NvmlDevice, NvmlUtilization *) = nullptr;
	NvmlReturn (*get_encoder_utilization)(NvmlDevice, unsigned int *, unsigned int *) = nullptr;
	bool initialized = false;

public:
	Nvml()
	{
		library = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
		if (!library) {
			return;
		}
		auto init = reinterpret_cast<NvmlReturn (*)()>(dlsym(library, "nvmlInit_v2"));
		shutdown = reinterpret_cast<NvmlReturn (*)()>(dlsym(library, "nvmlShutdown"));
		get_count = reinterpret_cast<NvmlReturn (*)(unsigned int *)>(dlsym(library, "nvmlDeviceGetCount_v2"));
		get_device = reinterpret_cast<NvmlReturn (*)(unsigned int, NvmlDevice *)>(
			dlsym(library, "nvmlDeviceGetHandleByIndex_v2"));
		get_utilization = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlUtilization *)>(
			dlsym(library, "nvmlDeviceGetUtilizationRates"));
		get_encoder_utilization = reinterpret_cast<NvmlReturn (*)(NvmlDevice, unsigned int *, unsigned int *)>(
			dlsym(library, "nvmlDeviceGetEncoderUtilization"));
		if (!init || !shutdown || !get_count || !get_device || !get_utilization || init() != 0) {
			return;
		}
		initialized = true;
	}

	~Nvml()
	{
		if (initialized) {
			shutdown();
		}
		if (library) {
			dlclose(library);
		}
	}

	void Read(std::vector<double> &gpu, std::vector<double> &video_encode) const
	{
		if (!initialized) {
			return;
		}
		unsigned int device_count = 0;
		if (get_count(&device_count) != 0) {
			return;
		}
		for (unsigned int index = 0; index < device_count; ++index) {
			NvmlDevice device = nullptr;
			if (get_device(index, &device) != 0 || !device) {
				continue;
			}
			NvmlUtilization utilization{};
			if (get_utilization(device, &utilization) == 0 && utilization.gpu <= 100) {
				gpu.push_back(static_cast<double>(utilization.gpu));
			}
			if (get_encoder_utilization) {
				unsigned int encode_percent = 0;
				unsigned int sampling_period_ms = 0;
				if (get_encoder_utilization(device, &encode_percent, &sampling_period_ms) == 0 &&
				    encode_percent <= 100) {
					video_encode.push_back(static_cast<double>(encode_percent));
				}
			}
		}
	}
};

class LinuxSampler final : public Sampler {
	Nvml nvml;

public:
	Sample Read() override
	{
		std::vector<double> gpu_readings;
		std::vector<double> encode_readings;
		const std::filesystem::path drm_root("/sys/class/drm");
		std::error_code error;
		for (std::filesystem::directory_iterator entry(drm_root, error), end; !error && entry != end;
		     entry.increment(error)) {
			const std::string name = entry->path().filename().string();
			if (name.size() < 5 || name.compare(0, 4, "card") != 0 ||
			    name.find_first_not_of("0123456789", 4) != std::string::npos) {
				continue;
			}
			std::ifstream file(entry->path() / "device" / "gpu_busy_percent");
			if (!file) {
				continue;
			}
			std::string text;
			std::getline(file, text);
			if (const auto percentage = ParseAmdGpuBusyPercent(text)) {
				gpu_readings.push_back(*percentage);
			}
		}
		nvml.Read(gpu_readings, encode_readings);
		/* No Intel provider is available here yet; its GPU field stays absent. */
		return {AveragePercentages(gpu_readings), std::nullopt, std::nullopt,
			AverageNvmlVideoEncodeUtilization(encode_readings)};
	}
};

} // namespace

std::unique_ptr<Sampler> CreatePlatformSampler()
{
	return std::make_unique<LinuxSampler>();
}

} // namespace preflight::gpu
