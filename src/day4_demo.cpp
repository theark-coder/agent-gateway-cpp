#include "agent/agent_loop.hpp"
#include "agent/python_model_adapter.hpp"
#include "agent/tool.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: agent_day4_demo <fake_model.py> <prompt> [actor_id] [session_id]\n";
        return 2;
    }
    agent::ToolRegistry tools;
    tools.register_tool(std::make_unique<agent::LookupPolicyTool>());
    agent::SessionMemory memory;
    agent::PythonModelAdapter model(argv[1]);
    agent::AgentLoop loop(model, tools, memory);
    const auto result = loop.run(argc > 3 ? argv[3] : "local-dev",
                                 argc > 4 ? argv[4] : "demo", argv[2],
                                 std::chrono::milliseconds(2000));
    if (!result.ok) {
        std::cerr << "ERROR " << result.error << " steps=" << result.steps
                  << " tools=" << result.tool_calls << '\n';
        return 1;
    }
    std::cout << "ANSWER: " << result.answer << "\nsteps=" << result.steps
              << " tools=" << result.tool_calls << '\n';
    return 0;
}
