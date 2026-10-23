#include <iostream>
#include "agent/health.hpp"

int main() {
    if (agent::health_message() != "agent-gateway-cpp Day1 ready") {
        std::cerr << "health test failed\n";
        return 1;
    }
    std::cout << "health test passed\n";
    return 0;
}
