#include "preflight-test-mode.hpp"

#include <string_view>

namespace preflight {

namespace {

bool has_parameter(std::string_view query, std::string_view parameter)
{
	while (!query.empty()) {
		const size_t separator = query.find('&');
		const std::string_view item = query.substr(0, separator);
		if (item == parameter) {
			return true;
		}
		if (separator == std::string_view::npos) {
			break;
		}
		query.remove_prefix(separator + 1);
	}
	return false;
}

} // namespace

UplinkTestMode uplink_test_mode_for_service(std::string_view service_name)
{
	if (service_name == "Twitch") {
		return UplinkTestMode::TwitchBandwidth;
	}
	if (service_name == "Amazon IVS") {
		return UplinkTestMode::AmazonIvsBandwidth;
	}
	if (service_name == "Restream.io" || service_name == "Restream.io - RTMP") {
		return UplinkTestMode::RestreamBandwidth;
	}
	if (service_name == "YouTube - RTMP" || service_name == "YouTube - RTMPS" || service_name == "YouTube - HLS") {
		return UplinkTestMode::YouTubeThrowawayStream;
	}
	return UplinkTestMode::CloudflareSpeedOnly;
}

std::optional<std::string> make_non_public_test_key(std::string_view service_name, std::string_view key)
{
	if (key.empty()) {
		return std::nullopt;
	}

	std::string_view test_parameter;
	switch (uplink_test_mode_for_service(service_name)) {
	case UplinkTestMode::TwitchBandwidth:
	case UplinkTestMode::AmazonIvsBandwidth:
		test_parameter = "bandwidthtest";
		break;
	case UplinkTestMode::RestreamBandwidth:
		test_parameter = "test=true";
		break;
	case UplinkTestMode::YouTubeThrowawayStream:
	case UplinkTestMode::CloudflareSpeedOnly:
		return std::nullopt;
	}

	const size_t query_start = key.find('?');
	if (query_start != std::string_view::npos && has_parameter(key.substr(query_start + 1), test_parameter)) {
		return std::string(key);
	}

	std::string test_key(key);
	test_key += query_start == std::string_view::npos ? '?' : '&';
	test_key += test_parameter;
	return test_key;
}

} // namespace preflight
