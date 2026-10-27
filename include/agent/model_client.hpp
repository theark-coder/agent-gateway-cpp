
#pragma once

#include <string>

namespace agent {

class IModelClient {
public:
    virtual ~IModelClient() = default;

    virtual std::string generate(
        const std::string& prompt
    ) const = 0;
};

class FakeModelClient final : public IModelClient {
public:
    std::string generate(
        const std::string& prompt
    ) const override;
};

}  // namespace agent
