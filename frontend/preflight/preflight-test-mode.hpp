#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace preflight {

enum class UplinkTestMode {
	TwitchBandwidth,
	AmazonIvsBandwidth,
	RestreamBandwidth,
	YouTubeThrowawayStream,
	CloudflareSpeedOnly,
};

/* Select only OBS's established non-public test modes. Services without one
 * use a speed test that never creates an OBS stream output. */
UplinkTestMode uplink_test_mode_for_service(std::string_view service_name);

/* Return a key that selects a service's documented, non-public test mode.
 * YouTube obtains its one-use key from the signed-in account instead. */
std::optional<std::string> make_non_public_test_key(std::string_view service_name, std::string_view key);

} // namespace preflight
