#pragma once

#include "agent/agent_loop.hpp"

#include <string>

namespace agent {

// Linux/WSL: one short-lived Python subprocess per decision, bounded by deadline.
class PythonModelAdapter final : public IAgentModel {
public:
    explicit PythonModelAdapter(std::string script_path);
    ModelDecision decide(const std::string& prompt, int step,
                         const std::string& tool_result,
                         const std::vector<MemoryMessage>& history,
                         std::chrono::steady_clock::time_point deadline) override;
private:
    std::string script_path_;
};

} // namespace agent
