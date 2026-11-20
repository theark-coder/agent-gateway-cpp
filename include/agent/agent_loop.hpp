#pragma once

#include "agent/tool.hpp"

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace agent {

struct MemoryMessage {
    std::string role;
    std::string content;
};

class SessionMemory final {
public:
    explicit SessionMemory(std::size_t max_messages = 20);
    std::vector<MemoryMessage> history(const std::string& actor_id,
                                       const std::string& session_id) const;
    void remember(const std::string& actor_id, const std::string& session_id,
                  const std::string& prompt, const std::string& answer);
private:
    const std::size_t limit_;
    mutable std::mutex mutex_;
    std::map<std::pair<std::string, std::string>, std::vector<MemoryMessage>> records_;
};

enum class ModelAction { final_answer, tool_call };

struct ModelDecision {
    ModelAction action = ModelAction::final_answer;
    std::string content;      // final_answer
    std::string tool_name;    // tool_call
    std::string tool_input;   // tool_call
};

class IAgentModel {
public:
    virtual ~IAgentModel() = default;
    virtual ModelDecision decide(const std::string& prompt, int step,
                                 const std::string& tool_result,
                                 const std::vector<MemoryMessage>& history,
                                 std::chrono::steady_clock::time_point deadline) = 0;
};

struct AgentRunResult {
    bool ok = false;
    std::string answer;
    std::string error;
    int steps = 0;
    int tool_calls = 0;
};

class AgentLoop final {
public:
    AgentLoop(IAgentModel& model, const ToolRegistry& tools, SessionMemory& memory,
              int max_steps = 4, int max_tool_calls = 3);

    AgentRunResult run(const std::string& actor_id,
                       const std::string& session_id,
                       const std::string& prompt,
                       std::chrono::milliseconds timeout);
private:
    IAgentModel& model_;
    const ToolRegistry& tools_;
    SessionMemory& memory_;
    const int max_steps_;
    const int max_tool_calls_;
};

} // namespace agent
