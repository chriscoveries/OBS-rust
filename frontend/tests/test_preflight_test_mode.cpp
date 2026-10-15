#include "../preflight/preflight-test-mode.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

struct TestCase {
	const char *name;
	std::string service;
	std::string key;
	std::optional<std::string> expected;
};

int main()
{
	struct ModeCase {
		const char *name;
		std::string service;
		preflight::UplinkTestMode expected;
	};

	const std::vector<ModeCase> modes = {
		{"twitch bandwidth mode", "Twitch", preflight::UplinkTestMode::TwitchBandwidth},
		{"ivs bandwidth mode", "Amazon IVS", preflight::UplinkTestMode::AmazonIvsBandwidth},
		{"restream ingest mode", "Restream.io", preflight::UplinkTestMode::RestreamBandwidth},
		{"youtube rtmp throwaway mode", "YouTube - RTMP", preflight::UplinkTestMode::YouTubeThrowawayStream},
		{"youtube rtmps throwaway mode", "YouTube - RTMPS", preflight::UplinkTestMode::YouTubeThrowawayStream},
		{"youtube hls throwaway mode", "YouTube - HLS", preflight::UplinkTestMode::YouTubeThrowawayStream},
		{"kick uses cloudflare only", "Kick", preflight::UplinkTestMode::CloudflareSpeedOnly},
		{"rumble uses cloudflare only", "Rumble", preflight::UplinkTestMode::CloudflareSpeedOnly},
		{"custom uses cloudflare only", "Custom RTMP", preflight::UplinkTestMode::CloudflareSpeedOnly},
	};

	int failures = 0;
	for (const auto &test : modes) {
		const auto actual = preflight::uplink_test_mode_for_service(test.service);
		if (actual != test.expected) {
			std::cerr << "FAIL " << test.name << '\n';
			++failures;
		} else {
			std::cout << "PASS " << test.name << '\n';
		}
	}

	const std::vector<TestCase> cases = {
		{"twitch bandwidth test", "Twitch", "live_example", "live_example?bandwidthtest"},
		{"preserve existing query", "Amazon IVS", "key?region=west", "key?region=west&bandwidthtest"},
		{"avoid duplicate bandwidth flag", "Twitch", "key?bandwidthtest", "key?bandwidthtest"},
		{"restream test mode", "Restream.io", "key", "key?test=true"},
		{"reject youtube live key", "YouTube", "key", std::nullopt},
		{"reject youtube rtmp key", "YouTube - RTMP", "key", std::nullopt},
		{"reject cloudflare stream key", "Kick", "key", std::nullopt},
		{"reject unknown service", "Custom", "key", std::nullopt},
		{"reject empty key", "Twitch", "", std::nullopt},
	};

	for (const auto &test : cases) {
		const auto actual = preflight::make_non_public_test_key(test.service, test.key);
		if (actual != test.expected) {
			std::cerr << "FAIL " << test.name << '\n';
			++failures;
		} else {
			std::cout << "PASS " << test.name << '\n';
		}
	}

	return failures == 0 ? 0 : 1;
}
