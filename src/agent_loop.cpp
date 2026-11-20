#include "agent/agent_loop.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace agent {

SessionMemory::SessionMemory(std::size_t max_messages) : limit_(max_messages) {
    if (limit_ < 2) throw std::invalid_argument("memory capacity must be >= 2");
}

std::vector<MemoryMessage> SessionMemory::history(
    const std::string& actor_id, const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find({actor_id, session_id});
    return it == records_.end() ? std::vector<MemoryMessage>{} : it->second;
}

void SessionMemory::remember(const std::string& actor_id,
                             const std::string& session_id,
                             const std::string& prompt, const std::string& answer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& messages = records_[{actor_id, session_id}];
    messages.push_back({"user", prompt});
    messages.push_back({"assistant", answer});
    if (messages.size() > limit_) {
        messages.erase(messages.begin(),
                       messages.begin() + static_cast<std::ptrdiff_t>(messages.size() - limit_));
    }
}

AgentLoop::AgentLoop(IAgentModel& model, const ToolRegistry& tools, SessionMemory& memory,
                     int max_steps, int max_tool_calls)
    : model_(model), tools_(tools), memory_(memory),
      max_steps_(max_steps), max_tool_calls_(max_tool_calls) {
    if (max_steps_ <= 0 || max_tool_calls_ < 0)
        throw std::invalid_argument("invalid agent budgets");
}

AgentRunResult AgentLoop::run(const std::string& actor_id,
                             const std::string& session_id,
                             const std::string& prompt,
                             std::chrono::milliseconds timeout) {
    AgentRunResult out;
    if (actor_id.empty() || session_id.empty() || actor_id.size() > 64 ||
        session_id.size() > 128 || prompt.empty() || prompt.size() > 2048 ||
        timeout.count() <= 0) {
        out.error = "invalid_request";
        return out;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto history = memory_.history(actor_id, session_id);
    std::string tool_result;
    for (int step = 0; step < max_steps_; ++step) {
        if (std::chrono::steady_clock::now() >= deadline) {
            out.error = "deadline_exceeded";
            return out;
        }
        ModelDecision decision;
        try {
            decision = model_.decide(prompt, step, tool_result, history, deadline);
        } catch (const std::exception&) {
            out.error = std::chrono::steady_clock::now() >= deadline ?
                "deadline_exceeded" : "model_unavailable";
            return out;
        }
        ++out.steps;
        if (std::chrono::steady_clock::now() >= deadline) {
            out.error = "deadline_exceeded";
            return out;
        }
        if (decision.action == ModelAction::final_answer) {
            if (decision.content.empty()) {
                out.error = "empty_model_answer";
                return out;
            }
            out.ok = true;
            out.answer = std::move(decision.content);
            memory_.remember(actor_id, session_id, prompt, out.answer);
            return out;
        }
        if (out.tool_calls >= max_tool_calls_) {
            out.error = "tool_budget_exceeded";
            return out;
        }
        if (decision.tool_name.empty() || decision.tool_input.empty() ||
            !tools_.has_tool(decision.tool_name)) {
            out.error = "unknown_or_invalid_tool";
            return out;
        }
        try {
            tool_result = tools_.execute(decision.tool_name, decision.tool_input);
        } catch (const std::exception&) {
            out.error = "tool_execution_failed";
            return out;
        }
        ++out.tool_calls;
        if (std::chrono::steady_clock::now() >= deadline) {
            out.error = "deadline_exceeded";
            return out;
        }
        if (tool_result.rfind("error:", 0) == 0) {
            out.error = "tool_execution_failed";
            return out;
        }
    }
    out.error = "step_budget_exceeded";
    return out;
}

} // namespace agent
