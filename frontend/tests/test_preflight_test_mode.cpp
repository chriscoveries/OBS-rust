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
	const std::vector<TestCase> cases = {
		{"twitch bandwidth test", "Twitch", "live_example", "live_example?bandwidthtest"},
		{"preserve existing query", "Amazon IVS", "key?region=west", "key?region=west&bandwidthtest"},
		{"avoid duplicate bandwidth flag", "Twitch", "key?bandwidthtest", "key?bandwidthtest"},
		{"restream test mode", "Restream.io", "key", "key?test=true"},
		{"reject youtube live key", "YouTube", "key", std::nullopt},
		{"reject unknown service", "Custom", "key", std::nullopt},
		{"reject empty key", "Twitch", "", std::nullopt},
	};

	int failures = 0;
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
