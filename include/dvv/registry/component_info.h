#pragma once

// Component descriptors (spec §9.2, §9.3).

#include <concepts>
#include <cstdint>
#include <string_view>

namespace dvv {

enum class Kind : std::uint8_t {
    Source,
    FormatDetector,
    Exporter,
    Tool,
    Metric,
    Analyser,
    Processor
};

[[nodiscard]] std::string_view kindName(Kind kind) noexcept;

// What a component reports about itself. id and name refer to static storage (string literals).
// The id is unique among all components and is what session files and the CLI store.
struct ComponentInfo {
    std::string_view id;
    std::string_view name;
    Kind kind = Kind::Source;
    int version = 1;

    friend constexpr bool operator==(const ComponentInfo&, const ComponentInfo&) = default;
};

// The component kind every implementation of interface I must report (spec §9.3). Specialise it
// in the interface's header:
//     template <> struct dvv::InterfaceKind<dvv::media::ISourceProvider>
//         : std::integral_constant<dvv::Kind, dvv::Kind::Source> {};
// Without a specialisation the registry does not check the kind.
template <class I>
struct InterfaceKind {};

template <class I>
concept HasInterfaceKind = requires {
    { InterfaceKind<I>::value } -> std::convertible_to<Kind>;
};

}  // namespace dvv
