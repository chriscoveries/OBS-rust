#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace preflight {

/* Return a key that selects a service's documented, non-public test mode.
 * Unknown services fail closed; their speed-test-only path is added in P5. */
std::optional<std::string> make_non_public_test_key(std::string_view service_name, std::string_view key);

} // namespace preflight
