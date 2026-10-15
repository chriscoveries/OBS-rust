#include "../preflight/preflight-cloudflare-speed-test.hpp"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

struct TestCase {
	const char *name;
	uint64_t bytes;
	uint64_t elapsed_microseconds;
	bool has_result;
	double expected_mbps;
};

int main()
{
	int failures = 0;
	const auto plan = preflight::cloudflare_transfer_plan();
	if (plan.download_url != "https://speed.cloudflare.com/__down?bytes=4194304" ||
	    plan.upload_url != "https://speed.cloudflare.com/__up" ||
	    plan.download_bytes != preflight::CLOUDFLARE_MAX_TRANSFER_BYTES ||
	    plan.upload_bytes != preflight::CLOUDFLARE_MAX_TRANSFER_BYTES) {
		std::cerr << "FAIL fixed Cloudflare endpoints and transfer cap\n";
		++failures;
	} else {
		std::cout << "PASS fixed Cloudflare endpoints and transfer cap\n";
	}
	if (plan.download_bytes + plan.upload_bytes > 8 * 1024 * 1024) {
		std::cerr << "FAIL total transfer stays within 8 MiB\n";
		++failures;
	} else {
		std::cout << "PASS total transfer stays within 8 MiB\n";
	}

	const std::vector<TestCase> cases = {
		{"decimal megabits per second", 1'000'000, 1'000'000, true, 8.0},
		{"fast sample", 4'194'304, 16'384, true, 2048.0},
		{"zero bytes has no rate", 0, 500'000, false, 0.0},
		{"zero time has no rate", 1'000'000, 0, false, 0.0},
	};
	for (const auto &test : cases) {
		const auto rate = preflight::cloudflare_megabits_per_second(test.bytes, test.elapsed_microseconds);
		if (rate.has_value() != test.has_result || (rate && std::abs(*rate - test.expected_mbps) > 0.0001)) {
			std::cerr << "FAIL " << test.name << '\n';
			++failures;
		} else {
			std::cout << "PASS " << test.name << '\n';
		}
	}
	return failures == 0 ? 0 : 1;
}
