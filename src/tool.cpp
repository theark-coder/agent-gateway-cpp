
#include "agent/tool.hpp"

#include <utility>

namespace agent {

std::string LookupPolicyTool::name() const {
    return "lookup_policy";
}

std::string LookupPolicyTool::execute(
    const std::string& input
) const {
    if (input == "returns") {
        return "Demo return policy: unused items may be "
               "returned within 30 days with proof of purchase.";
    }

    if (input == "warranty") {
        return "Demo warranty policy: eligible products "
               "have a 1-year limited warranty.";
    }

    return "error: unknown policy topic: " + input;
}

bool ToolRegistry::register_tool(
    std::unique_ptr<ITool> tool
) {
    if (!tool) {
        return false;
    }

    const std::string tool_name = tool->name();

    if (tool_name.empty()) {
        return false;
    }

    if (tools_.find(tool_name) != tools_.end()) {
        return false;
    }

    tools_.emplace(tool_name, std::move(tool));
    return true;
}

std::string ToolRegistry::execute(
    const std::string& tool_name,
    const std::string& input
) const {
    const auto it = tools_.find(tool_name);

    if (it == tools_.end()) {
        return "error: unknown tool: " + tool_name;
    }

    return it->second->execute(input);
}

}  // namespace agent
