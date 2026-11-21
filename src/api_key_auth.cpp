#include "agent/api_key_auth.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace agent {
namespace {

bool visible_ascii(const std::string& key) {
    for (unsigned char c : key) {
        if (c < 0x21 || c > 0x7e) return false;
    }
    return true;
}

// Compare every supplied byte without an early exit. This is not a replacement
// for a audited cryptographic constant-time comparison in production.
bool slow_equal(const std::string& expected, const std::string& actual) noexcept {
    const auto max_len = std::max(expected.size(), actual.size());
    unsigned int difference = static_cast<unsigned int>(expected.size() != actual.size());
    for (std::size_t i = 0; i < max_len; ++i) {
        const auto left = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0U;
        const auto right = i < actual.size() ? static_cast<unsigned char>(actual[i]) : 0U;
        difference |= left ^ right;
    }
    return difference == 0;
}

} // namespace

ApiKeyAuth::ApiKeyAuth(std::optional<std::string> expected_key)
    : expected_key_(std::move(expected_key)) {
    if (expected_key_) {
        // If an operator explicitly configures a key, never silently disable auth.
        if (expected_key_->size() < 16 || expected_key_->size() > 256 ||
            !visible_ascii(*expected_key_)) {
            throw std::invalid_argument(
                "AGENT_API_KEY must be 16-256 visible ASCII characters");
        }
    }
}

ApiKeyAuth ApiKeyAuth::from_environment() {
    const char* value = std::getenv("AGENT_API_KEY");
    if (!value) return ApiKeyAuth(std::nullopt);
    return ApiKeyAuth(std::string(value));
}

bool ApiKeyAuth::enabled() const noexcept {
    return expected_key_.has_value();
}

bool ApiKeyAuth::requires_auth(std::string_view path) const noexcept {
    if (!enabled()) return false;
    return path == "/api/v1" || path.substr(0, 8) == "/api/v1/";
}

bool ApiKeyAuth::allows(const std::vector<std::string>& provided_keys) const noexcept {
    if (!enabled()) return true;
    if (provided_keys.size() != 1 || provided_keys[0].size() > 256) return false;
    return slow_equal(*expected_key_, provided_keys[0]);
}

} // namespace agent
