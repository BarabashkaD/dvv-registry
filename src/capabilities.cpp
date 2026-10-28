#include "dvv/registry/capabilities.h"

#include <algorithm>
#include <format>
#include <functional>

namespace dvv {

Capabilities::Capabilities(std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        add(name);
    }
}

void Capabilities::add(std::string_view name) {
    const auto position = std::ranges::lower_bound(names_, name, std::less<>{});
    if (position == names_.end() || *position != name) {
        names_.emplace(position, name);
    }
}

bool Capabilities::has(std::string_view name) const noexcept {
    return std::ranges::binary_search(names_, name, std::less<>{});
}

std::expected<void, Unavailable> require(const Capabilities& caps,
                                         std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        if (!caps.has(name)) {
            return std::unexpected(Unavailable{std::format("missing capability '{}'", name)});
        }
    }
    return {};
}

}  // namespace dvv
