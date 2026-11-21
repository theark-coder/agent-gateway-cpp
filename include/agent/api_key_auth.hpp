#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agent {

// Single-key gateway authentication for the Day 5 demo.
// This authenticates the caller of an API, NOT the owner of an individual task.
class ApiKeyAuth final {
public:
    explicit ApiKeyAuth(std::optional<std::string> expected_key);
    static ApiKeyAuth from_environment();

    bool enabled() const noexcept;
    bool requires_auth(std::string_view path) const noexcept;
    bool allows(const std::vector<std::string>& provided_keys) const noexcept;

private:
    std::optional<std::string> expected_key_;
};

} // namespace agent
