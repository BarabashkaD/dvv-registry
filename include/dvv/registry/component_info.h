#pragma once

// Component info (spec §9.2).

#include <string_view>

namespace dvv {

// What a component reports about itself. id and name refer to static storage (string literals).
// The id is unique among all components and is what session files and the CLI store.
struct ComponentInfo {
    std::string_view id;
    std::string_view name;
    int version = 1;

    friend constexpr bool operator==(const ComponentInfo&, const ComponentInfo&) = default;
};

}  // namespace dvv
