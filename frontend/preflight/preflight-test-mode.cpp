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

std::optional<std::string> make_non_public_test_key(std::string_view service_name, std::string_view key)
{
	if (key.empty()) {
		return std::nullopt;
	}

	std::string_view test_parameter;
	if (service_name == "Twitch" || service_name == "Amazon IVS") {
		test_parameter = "bandwidthtest";
	} else if (service_name == "Restream.io" || service_name == "Restream.io - RTMP") {
		test_parameter = "test=true";
	} else {
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
