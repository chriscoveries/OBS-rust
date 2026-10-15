#include "preflight-test-mode.hpp"

namespace preflight {

std::optional<std::string> make_non_public_test_key(std::string_view, std::string_view)
{
	return std::nullopt;
}

} // namespace preflight
