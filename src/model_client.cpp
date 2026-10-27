
#include "agent/model_client.hpp"

namespace agent {

std::string FakeModelClient::generate(
    const std::string& prompt
) const {
    return "fake-model response: " + prompt;
}

}  // namespace agent
