#include "../../preflight-gpu-sampler.hpp"

#include <pdh.h>
#include <pdhmsg.h>

// The RPC headers included by PDH define this macro; do not leak it into unity builds.
#ifdef interface
#undef interface
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace preflight::gpu {
namespace {

class WindowsSampler final : public Sampler {
	PDH_HQUERY query = nullptr;
	PDH_HCOUNTER counter = nullptr;

public:
	WindowsSampler()
	{
		if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
			query = nullptr;
			return;
		}
		if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) !=
		    ERROR_SUCCESS) {
			PdhCloseQuery(query);
			query = nullptr;
			counter = nullptr;
		}
	}

	~WindowsSampler() override
	{
		if (query) {
			PdhCloseQuery(query);
		}
	}

	Sample Read() override
	{
		if (!query || !counter || PdhCollectQueryData(query) != ERROR_SUCCESS) {
			return {};
		}
		DWORD byte_count = 0;
		DWORD item_count = 0;
		PDH_STATUS status =
			PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &byte_count, &item_count, nullptr);
		if (status != PDH_MORE_DATA || byte_count == 0) {
			return {};
		}
		std::vector<uint64_t> storage((static_cast<size_t>(byte_count) + sizeof(uint64_t) - 1) /
					      sizeof(uint64_t));
		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(storage.data());
		status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &byte_count, &item_count, items);
		if (status != ERROR_SUCCESS) {
			return {};
		}
		std::string fixture;
		for (DWORD index = 0; index < item_count; ++index) {
			if (items[index].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
			    items[index].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
				continue;
			}
			std::string instance;
			for (const wchar_t *character = items[index].szName; character && *character; ++character) {
				instance.push_back(*character < 0x80 ? static_cast<char>(*character) : '?');
			}
			fixture.append(instance);
			fixture.push_back('=');
			fixture.append(std::to_string(items[index].FmtValue.doubleValue));
			fixture.push_back('\n');
		}
		return ParsePdhGpuEngineCounters(fixture);
	}
};

} // namespace

std::unique_ptr<Sampler> CreatePlatformSampler()
{
	return std::make_unique<WindowsSampler>();
}

} // namespace preflight::gpu
