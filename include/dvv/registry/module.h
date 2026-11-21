#pragma once

// Loadable modules. A library that loads something at run time (FFmpeg, a backend) registers one
// module:
//
//     DVV_REGISTER_MODULE(FfmpegModule);
//
// ComponentRegistry::load() runs every registered module's load(), adds the capabilities they
// provide, and probes the components with them. The registry owns each module's state and drops
// it, in reverse load order, when the registry is destroyed. Components can outlive the registry:
// a module whose components need its state gives them their own shared_ptr to it (aliasing the
// handle), so the state ends with its last user.
//
// Rules for a module:
// - load() may run on several threads at once (two threads loading a registry each): it must be
//   thread-safe. It must not call ComponentRegistry::load().
// - The handle's deleter runs on the thread that destroys the registry, inside its destructor: it
//   must not throw and must not call into the registry.
//
// ComponentRegistry::modules() and module(id) refer to statuses the registry owns: references,
// pointers and property() views stay valid as long as the registry lives.

#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/registry/parameters.h"

namespace dvv {

class ComponentRegistry;

// What a module reports about itself. id, name and produces refer to static storage.
struct ModuleInfo {
    std::string_view id{};    // unique among modules, e.g. "ffmpeg"
    std::string_view name{};  // for people, e.g. "FFmpeg"
    // The declared set: every capability load() may provide. `provides` is what load() gave.
    std::span<const std::string_view> produces{};
};

// One property a module learned at load, e.g. {"version", "n8.1.3"}. UTF-8 text, except
// MODULE_DIRECTORY_PROPERTY, which is path text (dvv::pathFromText of dvv-base 0.5.0 gives the
// folder; show it with dvv::pathDisplayText).
struct ModuleProperty {
    std::string key;
    std::string value;

    friend bool operator==(const ModuleProperty&, const ModuleProperty&) = default;
};

// Property keys with one meaning for every module; a module may add keys of its own.
inline constexpr std::string_view MODULE_VERSION_PROPERTY = "version";
inline constexpr std::string_view MODULE_LICENCE_PROPERTY = "licence";
// Path text of the folder the module loaded from. It may name the user's machine: mask it.
inline constexpr std::string_view MODULE_DIRECTORY_PROPERTY = "directory";
// The environment variable that chose that folder, when one did, e.g. "DVV_FFMPEG_DIR".
inline constexpr std::string_view MODULE_VARIABLE_PROPERTY = "variable";

// What a module's load() returns.
struct ModuleLoad {
    std::vector<std::string> provides{};  // each one of info().produces
    std::string reason{};  // why a produced capability is not provided; may hold a path
    std::vector<ModuleProperty> properties{};  // in the order the module wants them shown
    std::shared_ptr<void> handle{};            // the module's state; null when it keeps none
};

// What the registry knows about one module.
struct ModuleStatus {
    ModuleInfo info{};
    std::vector<std::string> provides{};  // in info().produces order; added to capabilities()
    std::vector<std::string> withheld{};  // info().produces minus provides, in the same order
    // Empty when load() returned and nothing is withheld; otherwise why. A failed module (its
    // load() threw, its id is registered more than once, or it provided a capability its info()
    // does not list) has an "Internal: ..." reason, also when it produces nothing.
    std::string reason{};
    std::vector<ModuleProperty> properties{};

    // True when the module provides at least one capability. A module that produces nothing is
    // never available; it exists for its properties and its state, and it failed when its reason
    // is not empty.
    [[nodiscard]] bool available() const noexcept { return !provides.empty(); }
    // The value of the first property with `key`; nullopt when there is none. The view points into
    // this status, so it is valid while the status lives; on a temporary it does not compile.
    [[nodiscard]] std::optional<std::string_view> property(std::string_view key) const& noexcept;
    std::optional<std::string_view> property(std::string_view key) const&& = delete;
};

// Options for ComponentRegistry::load(): each module's values, by module id. A module checks its
// own keys; one without an entry gets empty values.
struct ModuleOptions {
    std::map<std::string, ParameterValues, std::less<>> modules{};
};

// T is a loadable module: static info() noexcept and load(const ParameterValues&).
template <class T>
concept Module = requires(const ParameterValues& options) {
    { T::info() } noexcept -> std::same_as<ModuleInfo>;
    { T::load(options) } -> std::same_as<ModuleLoad>;
};

namespace detail {

// One registered module; a static object created by DVV_REGISTER_MODULE. Its constructor links it
// into the process-wide module list during static initialisation (no allocation).
class ModuleRegistrationNode final {
public:
    template <class T>
        requires Module<T>
    explicit ModuleRegistrationNode(std::type_identity<T> type) noexcept;

    ModuleRegistrationNode(const ModuleRegistrationNode&) = delete;
    ModuleRegistrationNode& operator=(const ModuleRegistrationNode&) = delete;
    ModuleRegistrationNode(ModuleRegistrationNode&&) = delete;
    ModuleRegistrationNode& operator=(ModuleRegistrationNode&&) = delete;
    ~ModuleRegistrationNode() = default;

private:
    friend class dvv::ComponentRegistry;
    using InfoFunction = ModuleInfo (*)() noexcept;
    using LoadFunction = ModuleLoad (*)(const ParameterValues&);

    ModuleRegistrationNode(InfoFunction info, LoadFunction load) noexcept;  // links the node

    static constinit ModuleRegistrationNode* list_head;  // valid before any registration runs

    InfoFunction info_;
    LoadFunction load_;
    ModuleRegistrationNode* next_;
};

template <class T>
    requires Module<T>
ModuleRegistrationNode::ModuleRegistrationNode(std::type_identity<T> /*type*/) noexcept
    : ModuleRegistrationNode([]() noexcept { return T::info(); },
                             [](const ParameterValues& options) { return T::load(options); }) {}

}  // namespace detail

}  // namespace dvv

// Registers module Type; use once at namespace scope in a .cpp of a static component library
// linked with dvv_link_components(), followed by a semicolon.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define DVV_REGISTER_MODULE(Type)                                                               \
    static_assert(::dvv::Module<Type>, "DVV_REGISTER_MODULE: " #Type                            \
                                       " must provide static info() noexcept -> "               \
                                       "dvv::ModuleInfo and load(const dvv::ParameterValues&) " \
                                       "-> dvv::ModuleLoad");                                   \
    static const ::dvv::detail::ModuleRegistrationNode DVV_DETAIL_CONCAT(                       \
        dvv_module_registration_, __LINE__)(std::type_identity<Type>{})
// NOLINTEND(bugprone-macro-parentheses)
