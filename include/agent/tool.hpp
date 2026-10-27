
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace agent {

class ITool {
public:
    virtual ~ITool() = default;

    virtual std::string name() const = 0;

    virtual std::string execute(
        const std::string& input
    ) const = 0;
};

class LookupPolicyTool final : public ITool {
public:
    std::string name() const override;

    std::string execute(
        const std::string& input
    ) const override;
};

class ToolRegistry final {
public:
    bool register_tool(std::unique_ptr<ITool> tool);

    std::string execute(
        const std::string& tool_name,
        const std::string& input
    ) const;

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>>
        tools_;
};

}  // namespace agent
