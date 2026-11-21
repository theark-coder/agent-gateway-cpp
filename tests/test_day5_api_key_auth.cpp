#include "agent/api_key_auth.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    using agent::ApiKeyAuth;
    ApiKeyAuth disabled(std::nullopt);
    assert(!disabled.enabled());
    assert(!disabled.requires_auth("/api/v1/tasks"));
    assert(disabled.allows({}));

    const std::string secret = "day5-test-secret-123456789";
    ApiKeyAuth enabled(secret);
    assert(enabled.enabled());
    assert(enabled.requires_auth("/api/v1"));
    assert(enabled.requires_auth("/api/v1/tasks"));
    assert(enabled.requires_auth("/api/v1/tasks/123"));
    assert(!enabled.requires_auth("/health"));
    assert(!enabled.requires_auth("/api/v10/tasks"));
    assert(!enabled.requires_auth("/api/v1evil"));
    assert(!enabled.allows({}));
    assert(!enabled.allows({"incorrect-token-00000000"}));
    assert(!enabled.allows({secret + "z"}));
    assert(!enabled.allows({secret, secret}));
    assert(enabled.allows({secret}));

    for (const auto& invalid : {std::string(""), std::string("short"),
                                std::string(257, 'x'), std::string("space in secret 1234")}) {
        bool thrown = false;
        try { ApiKeyAuth x(invalid); }
        catch (const std::invalid_argument&) { thrown = true; }
        assert(thrown);
    }
    std::cout << "PASS: api key validation, route boundaries, duplicate headers\n";
}
