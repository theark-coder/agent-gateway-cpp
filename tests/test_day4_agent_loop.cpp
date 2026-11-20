#include "agent/agent_loop.hpp"
#include "agent/tool.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using Clock = std::chrono::steady_clock;

class StubModel final : public agent::IAgentModel {
public:
    enum class Mode { one_tool, always_tool, final, unknown_tool, unavailable, slow };
    explicit StubModel(Mode m) : mode(m) {}
    agent::ModelDecision decide(const std::string&, int step,
                                const std::string& tool_result,
                                const std::vector<agent::MemoryMessage>&,
                                Clock::time_point deadline) override {
        if (mode == Mode::unavailable) throw std::runtime_error("offline");
        if (mode == Mode::slow) {
            while (Clock::now() < deadline) {}
            return {agent::ModelAction::final_answer, "too late", "", ""};
        }
        if (mode == Mode::final) return {agent::ModelAction::final_answer,"hello", "", ""};
        if (mode == Mode::unknown_tool)
            return {agent::ModelAction::tool_call, "", "does_not_exist", "test"};
        if (mode == Mode::always_tool || step == 0)
            return {agent::ModelAction::tool_call, "", "lookup_policy", "returns"};
        return {agent::ModelAction::final_answer, tool_result, "", ""};
    }
    Mode mode;
};

int main() {
    agent::ToolRegistry tools;
    assert(tools.register_tool(std::make_unique<agent::LookupPolicyTool>()));
    agent::SessionMemory memory(4);
    StubModel stub(StubModel::Mode::one_tool);
    agent::AgentLoop loop(stub, tools, memory);

    auto r = loop.run("user-a", "session-a", "policy: returns", std::chrono::seconds(1));
    assert(r.ok && r.steps == 2 && r.tool_calls == 1);
    assert(r.answer.find("30 days") != std::string::npos);
    assert(memory.history("user-a", "session-a").size() == 2);
    assert(memory.history("user-b", "session-a").empty());
    assert(memory.history("user-a", "session-b").empty());

    r = loop.run("user-a", "session-a", "policy: returns", std::chrono::seconds(1));
    assert(r.ok && memory.history("user-a", "session-a").size() == 4);
    r = loop.run("user-a", "session-a", "policy: returns", std::chrono::seconds(1));
    assert(r.ok && memory.history("user-a", "session-a").size() == 4);

    stub.mode = StubModel::Mode::always_tool;
    r = loop.run("user-a", "session-a", "loop", std::chrono::seconds(1));
    assert(!r.ok && r.error == "tool_budget_exceeded" && r.tool_calls == 3 && r.steps == 4);
    agent::AgentLoop step_limited(stub, tools, memory, 2, 3);
    r = step_limited.run("user-a", "session-a", "loop", std::chrono::seconds(1));
    assert(!r.ok && r.error == "step_budget_exceeded" && r.steps == 2);

    stub.mode = StubModel::Mode::unknown_tool;
    r = loop.run("user-a", "session-a", "bad", std::chrono::seconds(1));
    assert(!r.ok && r.error == "unknown_or_invalid_tool" && r.tool_calls == 0);
    stub.mode = StubModel::Mode::unavailable;
    r = loop.run("user-a", "session-a", "bad", std::chrono::seconds(1));
    assert(!r.ok && r.error == "model_unavailable");
    stub.mode = StubModel::Mode::slow;
    r = loop.run("user-a", "session-a", "slow", std::chrono::milliseconds(5));
    assert(!r.ok && r.error == "deadline_exceeded");
    r = loop.run("", "session-a", "bad", std::chrono::seconds(1));
    assert(!r.ok && r.error == "invalid_request");

    std::cout << "PASS: steps/tools/deadline/errors/session isolation/memory bound\n";
}
