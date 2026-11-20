#include "dvv/registry/module.h"

#include <algorithm>

namespace dvv {

std::optional<std::string_view> ModuleStatus::fact(std::string_view key) const& noexcept {
    const auto found =
        std::ranges::find_if(facts, [key](const ModuleFact& entry) { return entry.key == key; });
    if (found == facts.end()) {
        return std::nullopt;
    }
    return std::string_view{found->value};
}

namespace detail {

constinit ModuleRegistrationNode* ModuleRegistrationNode::list_head = nullptr;

ModuleRegistrationNode::ModuleRegistrationNode(InfoFunction info, LoadFunction load) noexcept
    : info_(info), load_(load), next_(list_head) {
    list_head = this;
}

}  // namespace detail

}  // namespace dvv
