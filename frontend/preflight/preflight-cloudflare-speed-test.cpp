#include "preflight-cloudflare-speed-test.hpp"

#include <curl/curl.h>

#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace preflight {

namespace {

// Cloudflare only needs a small acknowledgment body for an upload request.
constexpr uint64_t RESPONSE_BODY_LIMIT = 64 * 1024;
// Bound DNS/connect stalls and slow transfers so cancel/close always completes.
constexpr long CONNECT_TIMEOUT_SECONDS = 10;
constexpr long TRANSFER_TIMEOUT_SECONDS = 60;
std::once_flag curl_init_once;
CURLcode curl_init_status = CURLE_FAILED_INIT;

struct TransferState {
	const std::atomic<bool> *cancel_requested;
	uint64_t byte_limit;
	uint64_t received = 0;
};

size_t receive_bounded(void *, size_t size, size_t count, void *user_data)
{
	auto *state = static_cast<TransferState *>(user_data);
	if (!state || (size != 0 && count > std::numeric_limits<size_t>::max() / size)) {
		return 0;
	}
	const size_t amount = size * count;
	if (state->cancel_requested->load(std::memory_order_relaxed) || amount > state->byte_limit - state->received) {
		return 0;
	}
	state->received += amount;
	return amount;
}

int report_progress(void *user_data, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *cancel_requested = static_cast<const std::atomic<bool> *>(user_data);
	return cancel_requested->load(std::memory_order_relaxed) ? 1 : 0;
}

template<typename T> bool set_option(CURL *curl, CURLoption option, T value, char *error_buffer, std::string &error)
{
	const CURLcode status = curl_easy_setopt(curl, option, value);
	if (status == CURLE_OK) {
		return true;
	}
	error = error_buffer && *error_buffer ? error_buffer : curl_easy_strerror(status);
	return false;
}

struct TransferResult {
	uint64_t bytes = 0;
	uint64_t elapsed_microseconds = 0;
	std::string error;
	bool cancelled = false;
};

TransferResult perform_transfer(const std::string_view url, bool upload, const std::vector<char> &upload_body,
				uint64_t expected_download_bytes, const std::atomic<bool> &cancel_requested)
{
	TransferResult result;
	if (cancel_requested.load(std::memory_order_relaxed)) {
		result.cancelled = true;
		return result;
	}

	std::call_once(curl_init_once, [] { curl_init_status = curl_global_init(CURL_GLOBAL_DEFAULT); });
	if (curl_init_status != CURLE_OK) {
		result.error = "Could not initialize HTTPS support for the speed test.";
		return result;
	}
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
	if (!curl) {
		result.error = "Could not initialize the HTTPS speed test.";
		return result;
	}
	char error_buffer[CURL_ERROR_SIZE] = {};
	TransferState transfer{&cancel_requested, upload ? RESPONSE_BODY_LIMIT : expected_download_bytes};
	std::string option_error;
	const auto set = [&](CURLoption option, auto value) {
		return set_option(curl.get(), option, value, error_buffer, option_error);
	};
	if (!set(CURLOPT_URL, url.data()) || !set(CURLOPT_ERRORBUFFER, error_buffer) ||
	    !set(CURLOPT_PROTOCOLS_STR, "https") || !set(CURLOPT_FOLLOWLOCATION, 0L) || !set(CURLOPT_NOSIGNAL, 1L) ||
	    !set(CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS) || !set(CURLOPT_TIMEOUT, TRANSFER_TIMEOUT_SECONDS) ||
	    !set(CURLOPT_NOPROGRESS, 0L) || !set(CURLOPT_XFERINFOFUNCTION, &report_progress) ||
	    !set(CURLOPT_XFERINFODATA, &cancel_requested) || !set(CURLOPT_WRITEFUNCTION, &receive_bounded) ||
	    !set(CURLOPT_WRITEDATA, &transfer) || !set(CURLOPT_USERAGENT, "OBS-Studio-Preflight/1")) {
		result.error = std::move(option_error);
		return result;
	}

	curl_slist *headers = nullptr;
	std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_guard(nullptr, curl_slist_free_all);
	if (upload) {
		headers = curl_slist_append(nullptr, "Content-Type: application/octet-stream");
		if (!headers) {
			result.error = "Could not prepare the upload request.";
			return result;
		}
		header_guard.reset(headers);
		if (!set(CURLOPT_HTTPHEADER, headers) || !set(CURLOPT_POST, 1L) ||
		    !set(CURLOPT_POSTFIELDS, upload_body.data()) ||
		    !set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(upload_body.size()))) {
			result.error = std::move(option_error);
			return result;
		}
	}

	const CURLcode status = curl_easy_perform(curl.get());
	if (cancel_requested.load(std::memory_order_relaxed)) {
		result.cancelled = true;
		return result;
	}
	if (status != CURLE_OK) {
		result.error = *error_buffer ? error_buffer : curl_easy_strerror(status);
		return result;
	}

	long response_code = 0;
	const CURLcode info_status = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
	if (info_status != CURLE_OK || response_code < 200 || response_code >= 300) {
		result.error = "Cloudflare did not accept the speed test request.";
		return result;
	}
	curl_off_t total_time = 0;
	curl_off_t payload_start_time = 0;
	const CURLINFO payload_start_info = upload ? CURLINFO_PRETRANSFER_TIME_T : CURLINFO_STARTTRANSFER_TIME_T;
	if (curl_easy_getinfo(curl.get(), CURLINFO_TOTAL_TIME_T, &total_time) != CURLE_OK ||
	    curl_easy_getinfo(curl.get(), payload_start_info, &payload_start_time) != CURLE_OK || total_time < 0 ||
	    payload_start_time < 0) {
		result.error = "Could not measure Cloudflare transfer time.";
		return result;
	}
	// Exclude DNS/TLS setup for downloads and measure from the ready-to-send point for uploads.
	result.elapsed_microseconds =
		static_cast<uint64_t>(total_time > payload_start_time ? total_time - payload_start_time : 1);
	if (upload) {
		curl_off_t uploaded_bytes = 0;
		if (curl_easy_getinfo(curl.get(), CURLINFO_SIZE_UPLOAD_T, &uploaded_bytes) != CURLE_OK ||
		    uploaded_bytes < 0) {
			result.error = "Could not measure the Cloudflare upload sample.";
			return result;
		}
		result.bytes = static_cast<uint64_t>(uploaded_bytes);
	} else {
		result.bytes = transfer.received;
	}
	if (!upload && result.bytes != expected_download_bytes) {
		result.error = "Cloudflare returned fewer bytes than the requested download sample.";
		return result;
	}
	if (upload && result.bytes != upload_body.size()) {
		result.error = "Cloudflare accepted fewer bytes than the requested upload sample.";
		return result;
	}
	return result;
}

} // namespace

CloudflareTransferPlan cloudflare_transfer_plan() noexcept
{
	return {
		.download_url = "https://speed.cloudflare.com/__down?bytes=4194304",
		.upload_url = "https://speed.cloudflare.com/__up",
		.download_bytes = CLOUDFLARE_MAX_TRANSFER_BYTES,
		.upload_bytes = CLOUDFLARE_MAX_TRANSFER_BYTES,
	};
}

std::optional<double> cloudflare_megabits_per_second(uint64_t bytes, uint64_t elapsed_microseconds) noexcept
{
	if (bytes == 0 || elapsed_microseconds == 0) {
		return std::nullopt;
	}
	return static_cast<double>(bytes) * 8.0 / static_cast<double>(elapsed_microseconds);
}

CloudflareSpeedTest::~CloudflareSpeedTest()
{
	Cancel();
	if (worker.joinable()) {
		worker.join();
	}
}

bool CloudflareSpeedTest::Start()
{
	if (running.load(std::memory_order_acquire)) {
		return false;
	}
	if (worker.joinable()) {
		worker.join();
	}
	{
		std::lock_guard lock(result_mutex);
		result.reset();
	}
	cancel_requested.store(false, std::memory_order_release);
	running.store(true, std::memory_order_release);
	try {
		worker = std::thread(&CloudflareSpeedTest::Run, this);
	} catch (const std::exception &) {
		running.store(false, std::memory_order_release);
		return false;
	}
	return true;
}

void CloudflareSpeedTest::Cancel()
{
	cancel_requested.store(true, std::memory_order_release);
}

bool CloudflareSpeedTest::IsRunning() const noexcept
{
	return running.load(std::memory_order_acquire);
}

std::optional<CloudflareSpeedResult> CloudflareSpeedTest::TakeResult()
{
	if (running.load(std::memory_order_acquire)) {
		return std::nullopt;
	}
	if (worker.joinable()) {
		worker.join();
	}
	std::lock_guard lock(result_mutex);
	return std::exchange(result, std::nullopt);
}

void CloudflareSpeedTest::Run()
{
	CloudflareSpeedResult final_result;
	try {
		const CloudflareTransferPlan plan = cloudflare_transfer_plan();
		std::vector<char> upload_body(static_cast<size_t>(plan.upload_bytes));
		uint32_t state = UINT32_C(0x9e3779b9);
		for (char &byte : upload_body) {
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			byte = static_cast<char>(state & 0xff);
		}

		const TransferResult download =
			perform_transfer(plan.download_url, false, upload_body, plan.download_bytes, cancel_requested);
		if (download.cancelled) {
			final_result.cancelled = true;
		} else if (!download.error.empty()) {
			final_result.error = download.error;
		} else {
			const auto download_rate =
				cloudflare_megabits_per_second(download.bytes, download.elapsed_microseconds);
			if (!download_rate) {
				final_result.error = "Cloudflare returned no measurable download data.";
			} else {
				final_result.download_mbps = *download_rate;
				const TransferResult upload = perform_transfer(plan.upload_url, true, upload_body,
									       plan.upload_bytes, cancel_requested);
				if (upload.cancelled) {
					final_result.cancelled = true;
				} else if (!upload.error.empty()) {
					final_result.error = upload.error;
				} else if (const auto upload_rate = cloudflare_megabits_per_second(
						   upload.bytes, upload.elapsed_microseconds)) {
					final_result.upload_mbps = *upload_rate;
					final_result.success = true;
				} else {
					final_result.error = "Cloudflare accepted no measurable upload data.";
				}
			}
		}
	} catch (const std::exception &) {
		final_result.error = "The speed test could not prepare its bounded transfer data.";
	}
	{
		std::lock_guard lock(result_mutex);
		result = std::move(final_result);
	}
	running.store(false, std::memory_order_release);
}

} // namespace preflight
