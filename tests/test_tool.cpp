
#include "agent/model_client.hpp"
#include "agent/tool.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;

void check(bool passed, const char* name) {
    std::cout << '[' << (passed ? "PASS" : "FAIL")
              << "] " << name << '\n';

    if (!passed) {
        ++failures;
    }
}

class DestructionTrackingTool final : public agent::ITool {
public:
    explicit DestructionTrackingTool(bool& destroyed)
        : destroyed_(destroyed) {}

    ~DestructionTrackingTool() override {
        destroyed_ = true;
    }

    std::string name() const override {
        return "destruction_tracking";
    }

    std::string execute(
        const std::string& input
    ) const override {
        return input;
    }

private:
    bool& destroyed_;
};

bool test_lookup_policy_polymorphism() {
    agent::LookupPolicyTool concrete;
    const agent::ITool& tool = concrete;

    return tool.name() == "lookup_policy" &&
           tool.execute("returns") ==
               "Demo return policy: unused items may be "
               "returned within 30 days with proof of purchase." &&
           tool.execute("invalid") ==
               "error: unknown policy topic: invalid";
}

bool test_registry_dispatch_and_unknown_tool() {
    agent::ToolRegistry registry;

    if (!registry.register_tool(
            std::make_unique<agent::LookupPolicyTool>())) {
        return false;
    }

    if (registry.execute("lookup_policy", "warranty") !=
        "Demo warranty policy: eligible products "
        "have a 1-year limited warranty.") {
        return false;
    }

    return registry.execute("missing_tool", "anything") ==
           "error: unknown tool: missing_tool";
}

bool test_registry_rejects_invalid_registration() {
    agent::ToolRegistry registry;

    if (registry.register_tool(nullptr)) {
        return false;
    }

    if (!registry.register_tool(
            std::make_unique<agent::LookupPolicyTool>())) {
        return false;
    }

    // 不允许相同名称的工具覆盖已有工具
    if (registry.register_tool(
            std::make_unique<agent::LookupPolicyTool>())) {
        return false;
    }

    return registry.execute("lookup_policy", "returns") ==
           "Demo return policy: unused items may be "
           "returned within 30 days with proof of purchase.";
}

bool test_model_client_polymorphism() {
    agent::FakeModelClient fake;
    const agent::IModelClient& client = fake;

    return client.generate("hello") ==
           "fake-model response: hello";
}

bool test_virtual_destructor() {
    bool destroyed = false;

    {
        agent::ToolRegistry registry;

        if (!registry.register_tool(
                std::make_unique<DestructionTrackingTool>(
                    destroyed))) {
            return false;
        }

        if (destroyed) {
            return false;
        }
    }

    return destroyed;
}

}  // namespace

int main() {
    check(
        test_lookup_policy_polymorphism(),
        "ITool dynamic dispatch"
    );

    check(
        test_registry_dispatch_and_unknown_tool(),
        "ToolRegistry dispatch and unknown tool"
    );

    check(
        test_registry_rejects_invalid_registration(),
        "ToolRegistry rejects null and duplicate tools"
    );

    check(
        test_model_client_polymorphism(),
        "IModelClient dynamic dispatch"
    );

    check(
        test_virtual_destructor(),
        "polymorphic destruction through ITool"
    );

    std::cout << "Failures: " << failures << '\n';

    return failures == 0 ? 0 : 1;
}
