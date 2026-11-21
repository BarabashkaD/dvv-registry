#pragma once

// Capability probing: the environment reports named capabilities, and each component's probe()
// decides from them whether it can run. Each name is defined once, as a constant of the module that
// produces it.

#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace dvv {

// Why a component cannot run here; shown next to the greyed-out component.
struct Unavailable {
    std::string reason;
    // The missing capability when that is the cause (require() fills it); empty otherwise. Lets an
    // app find the module that withholds it (ComponentRegistry::modules()).
    std::string capability{};
};

// A set of capability names.
class Capabilities {
public:
    Capabilities() = default;
    Capabilities(std::initializer_list<std::string_view> names);

    void add(std::string_view name);
    [[nodiscard]] bool has(std::string_view name) const noexcept;
    // Sorted, without duplicates.
    [[nodiscard]] const std::vector<std::string>& names() const noexcept { return names_; }

private:
    std::vector<std::string> names_;
};

// Succeeds when caps has every name; otherwise names the first missing one in the reason and in
// Unavailable::capability.
[[nodiscard]] std::expected<void, Unavailable> require(
    const Capabilities& caps, std::initializer_list<std::string_view> names);

}  // namespace dvv
