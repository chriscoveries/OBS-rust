#include "../../frontend/components/VolumeMeterSnapshot.hpp"

#include <cstdlib>
#include <mutex>
#include <thread>

struct MeterData {
	std::mutex mutex;
	uint64_t lastUpdate = 0;
	float peak[4] = {};
	float peakHold[4] = {};
	float magnitude[4] = {};
	float inputPeakHold[4] = {};
};

int main()
{
	MeterData data;
	std::thread writer([&]() {
		for (int i = 0; i < 50000; i++) {
			std::lock_guard lock(data.mutex);
			data.lastUpdate = 1000000000;
			for (int channel = 0; channel < 4; channel++) {
				const float value = static_cast<float>(i);
				data.peak[channel] = value;
				data.peakHold[channel] = value;
				data.magnitude[channel] = value;
				data.inputPeakHold[channel] = value;
			}
		}
	});
	std::thread reader([&]() {
		for (int i = 0; i < 50000; i++) {
			auto levels = snapshotVolumeMeterChannels(data.mutex, data.peak, data.peakHold, data.magnitude,
								  data.inputPeakHold);
			for (const auto &level : levels) {
				if (level.peak != level.peakHold || level.peak != level.magnitude ||
				    level.peak != level.inputPeakHold) {
					std::abort();
				}
			}
			resetVolumeMeterIfIdle(data.mutex, data.lastUpdate, 1000000000,
					       [&]() { data.lastUpdate = 1000000000; });
		}
	});
	writer.join();
	reader.join();
}
