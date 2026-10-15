#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace preflight {

// Bound this speed-test-only action to 4 MiB in each direction (8 MiB total).
inline constexpr uint64_t CLOUDFLARE_MAX_TRANSFER_BYTES = 4 * 1024 * 1024;

struct CloudflareTransferPlan {
	std::string_view download_url;
	std::string_view upload_url;
	uint64_t download_bytes;
	uint64_t upload_bytes;
};

struct CloudflareSpeedResult {
	bool cancelled = false;
	bool success = false;
	double download_mbps = 0.0;
	double upload_mbps = 0.0;
	std::string error;
};

CloudflareTransferPlan cloudflare_transfer_plan() noexcept;
std::optional<double> cloudflare_megabits_per_second(uint64_t bytes, uint64_t elapsed_microseconds) noexcept;

/* One bounded HTTPS download followed by one bounded HTTPS upload. */
class CloudflareSpeedTest final {
	std::atomic<bool> cancel_requested = false;
	std::atomic<bool> running = false;
	std::mutex result_mutex;
	std::optional<CloudflareSpeedResult> result;
	std::thread worker;

	void Run();

public:
	CloudflareSpeedTest() = default;
	CloudflareSpeedTest(const CloudflareSpeedTest &) = delete;
	CloudflareSpeedTest &operator=(const CloudflareSpeedTest &) = delete;
	~CloudflareSpeedTest();

	bool Start();
	void Cancel();
	bool IsRunning() const noexcept;
	std::optional<CloudflareSpeedResult> TakeResult();
};

} // namespace preflight
