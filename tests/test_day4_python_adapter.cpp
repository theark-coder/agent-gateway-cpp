#include "agent/agent_loop.hpp"
#include "agent/python_model_adapter.hpp"
#include "agent/tool.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    agent::ToolRegistry tools;
    assert(tools.register_tool(std::make_unique<agent::LookupPolicyTool>()));
    agent::SessionMemory memory;
    agent::PythonModelAdapter model(argv[1]);
    agent::AgentLoop loop(model, tools, memory);
    auto r = loop.run("u", "s", "policy: returns", std::chrono::seconds(3));
    assert(r.ok && r.steps == 2 && r.tool_calls == 1);
    assert(r.answer.find("30 days") != std::string::npos);
    r = loop.run("u", "s", "hello", std::chrono::seconds(3));
    assert(r.ok && r.answer == "FAKE: hello");
    std::cout << "PASS: C++ -> Python JSONL -> ToolRegistry -> final\n";
}
