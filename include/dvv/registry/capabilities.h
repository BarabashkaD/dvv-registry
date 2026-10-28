#pragma once

// Capability probing (spec §9.2): the environment reports named capabilities ("gpu.compute",
// "ffmpeg", ...) and each component's probe() decides from them whether it can run.

#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace dvv {

// Why a component cannot run here; shown next to the greyed-out component.
struct Unavailable {
    std::string reason;
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

// Succeeds when caps has every name; otherwise names the first missing one.
[[nodiscard]] std::expected<void, Unavailable> require(
    const Capabilities& caps, std::initializer_list<std::string_view> names);

}  // namespace dvv
