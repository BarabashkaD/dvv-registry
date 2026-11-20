#pragma once

// Components and the registry that creates them (spec §9.1, §9.2, §9.4).
//
// A component is a final class implementing one interface I (IMetric, ISource, ...), deriving
// Managed<Self>, with static info() and probe() and a constructor taking
// CreationKey<ComponentRegistry>. Deprecated (0.2.0): a component's schema() and the
// (CreationKey, const ParameterValues&) constructor; removed in 1.0.0. One line at namespace scope
// in its .cpp registers it:
//
//     DVV_REGISTER(IMetric, PsnrMetric);
//
// The line defines a static registration node that links itself into a process-wide list during
// static initialisation (no allocation). ComponentRegistry reads that list; it is the only creator
// of components. Component libraries are linked with dvv_link_components() (WHOLE_ARCHIVE), so the
// registration nodes survive linking. DVV_REGISTER_MODULE (dvv/registry/module.h) registers a
// module the same way.

#include <concepts>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_info.h"
#include "dvv/registry/module.h"
#include "dvv/registry/parameters.h"

namespace dvv {

class ComponentRegistry;

namespace detail {

template <class T>
concept DeclaresSchema = requires { T::schema(); };
template <class T>
concept TakesParameterValues =
    std::is_constructible_v<T, CreationKey<ComponentRegistry>, const ParameterValues&>;
// The schema of a component without schema(): no parameters. Deprecated with ParameterSchema.
[[nodiscard]] const ParameterSchema& noParameters() noexcept;

}  // namespace detail

// T implements interface I as a component (spec §9.2). info() must not throw: the registry needs
// the id to report anything else about the component. schema() is optional.
// Deprecated (0.2.0): a component's schema() and the (CreationKey, const ParameterValues&)
// constructor; removed in 1.0.0.
template <class T, class I>
concept Component = std::derived_from<T, I> && requires(const Capabilities& caps) {
    { T::info() } noexcept -> std::same_as<ComponentInfo>;
    { T::probe(caps) } -> std::same_as<std::expected<void, Unavailable>>;
} && (!detail::DeclaresSchema<T> || requires {
                        { T::schema() } -> std::same_as<const ParameterSchema&>;
                    });

namespace detail {

// The static functions of one registered component.
struct RegistrationHooks {
    ComponentInfo (*info)() noexcept;
    const ParameterSchema& (*schema)();  // never null; see noParameters()
    std::expected<void, Unavailable> (*probe)(const Capabilities&);
};

// One registered component; a static object created by DVV_REGISTER. The constructor links it
// into the process-wide list, so registration happens during static initialisation, before main.
class RegistrationNode {
public:
    RegistrationNode(const RegistrationNode&) = delete;
    RegistrationNode& operator=(const RegistrationNode&) = delete;
    RegistrationNode(RegistrationNode&&) = delete;
    RegistrationNode& operator=(RegistrationNode&&) = delete;

protected:
    RegistrationNode(const std::type_info& interface, std::optional<Kind> interface_kind,
                     RegistrationHooks hooks) noexcept;
    ~RegistrationNode() = default;

private:
    friend class dvv::ComponentRegistry;

    // Constant-initialised, so it is valid before any registration runs.
    static constinit RegistrationNode* list_head;

    const std::type_info* interface_;
    std::optional<Kind> interface_kind_;  // InterfaceKind<I>, when I specialises it
    RegistrationHooks hooks_;
    RegistrationNode* next_;
};

template <class I>
class ComponentRegistration final : public RegistrationNode {
public:
    template <class T>
    explicit ComponentRegistration(std::type_identity<T> type) noexcept;

private:
    friend class dvv::ComponentRegistry;
    using CreateFunction = std::unique_ptr<I> (*)(ObjectId owner, const ParameterValues& values,
                                                  std::source_location where);

    CreateFunction create_;
};

}  // namespace detail

// What the registry knows about one component.
struct ComponentStatus {
    ComponentInfo info;
    // Null when schema() threw; empty for a component without schema(). Deprecated (0.2.0): with
    // ParameterSchema; removed in 1.0.0.
    const ParameterSchema* schema = nullptr;
    // Empty when the component can be created.
    std::optional<Unavailable> unavailable = std::nullopt;

    [[nodiscard]] bool available() const noexcept { return !unavailable.has_value(); }
};

namespace detail {
// The module states a registry owns. Releases them in reverse load order when destroyed, also
// when load() fails after some modules ran. Internal (detail), so the tests can pin the order.
class ModuleHandles {
public:
    ModuleHandles() = default;
    ModuleHandles(ModuleHandles&&) noexcept = default;  // the moved-from one is empty
    ModuleHandles& operator=(ModuleHandles&&) = delete;
    ModuleHandles(const ModuleHandles&) = delete;
    ModuleHandles& operator=(const ModuleHandles&) = delete;
    ~ModuleHandles();                        // pops from the back
    void add(std::shared_ptr<void> handle);  // ignores null
private:
    std::vector<std::shared_ptr<void>> handles_;
};
}  // namespace detail

// The creation authority of every component (spec §9.4). Thread-safe.
class ComponentRegistry final : public Managed<ComponentRegistry> {
    struct Entry {
        const detail::RegistrationNode* registration;
        ComponentStatus status;
    };
    struct Parts {
        std::vector<Entry> entries{};         // sorted by id
        std::vector<ModuleStatus> modules{};  // sorted by id
        Capabilities capabilities{};          // holds a vector: moves without throwing
        detail::ModuleHandles handles{};      // in load order
    };

public:
    // Runs no module: probes with `caps` as given. For tests and hosts that state their
    // capabilities; modules() is empty and capabilities() is `caps`. `where` is the creation site
    // that leak reports show: the caller.
    //
    // Runs every registered component's probe(caps) once (and schema(), where a component still has
    // one). A component whose schema() or probe() throws is unavailable with an "Internal: ..."
    // reason, and so is every component whose id is registered more than once. A component whose
    // info().kind differs from InterfaceKind<I> of its interface is unavailable without being
    // probed.
    [[nodiscard]] static std::unique_ptr<ComponentRegistry> create(
        const Capabilities& caps, std::source_location where = std::source_location::current());

    // Runs every registered module's load(options.modules[id]) in id order, probes every
    // component with the union of what the modules provide, and keeps each module's status and
    // state. A module whose load() throws, whose id is registered more than once, or that
    // provides a capability its info() does not list provides nothing. Call from main, never
    // during static initialisation. Each call runs every module again, and two threads that call
    // load() run a module's load() at the same time: a module whose resource is process-wide keeps
    // it once (FFmpeg does). `where` as for create().
    [[nodiscard]] static std::unique_ptr<ComponentRegistry> load(
        const ModuleOptions& options = {},
        std::source_location where = std::source_location::current());

    // For create() and load() only (Parts is private; a CreationKey comes only from the registry).
    ComponentRegistry(CreationKey<ComponentRegistry> key, Parts parts) noexcept;

    // Modules sorted by id (copies of a duplicated id are all listed); empty after create().
    // Valid while the registry lives.
    [[nodiscard]] std::span<const ModuleStatus> modules() const noexcept;
    // The first module with `id`; nullptr when none is registered. Valid while the registry lives.
    [[nodiscard]] const ModuleStatus* module(std::string_view id) const noexcept;
    // The capabilities the components were probed with.
    [[nodiscard]] const Capabilities& capabilities() const noexcept;

    // Components sorted by id.
    [[nodiscard]] std::vector<ComponentStatus> list() const;
    // The components registered for interface I, sorted by id, available or not.
    template <class I>
    [[nodiscard]] std::vector<ComponentStatus> list() const {
        return listFor(typeid(I));
    }
    // Deprecated (0.2.0): a component's interface is its category; use list<I>(). Removed in 1.0.0.
    [[nodiscard]] std::vector<ComponentStatus> list(Kind kind) const;

    // Creates component `id`. Errc::Unsupported for an unknown or unavailable id or one with
    // another interface. std::bad_alloc from the constructor becomes Errc::OutOfMemory and leaves
    // the component available; any other constructor exception becomes Errc::Internal and makes the
    // component unavailable for this registry's lifetime. `where` is the creation site that leak
    // reports show: the caller. A constrained template, so a braced `{}` cannot deduce it and
    // createComponent<I>(id, {}) keeps the 0.1.1 form.
    template <class I, std::same_as<std::source_location> Where>
    [[nodiscard]] Result<std::unique_ptr<I>> createComponent(std::string_view id, Where where) {
        return createComponent<I>(id, ParameterValues{}, where);
    }

    // As createComponent(id, where), with `values` validated against the component's schema and
    // completed with the defaults: Errc::Format for invalid values.
    // Deprecated (0.2.0): the values argument; use createComponent(id, where) or
    // createComponent(id). Removed in 1.0.0, where createComponent(id, where = current()) is the
    // one form.
    template <class I>
    [[nodiscard]] Result<std::unique_ptr<I>> createComponent(
        std::string_view id, const ParameterValues& values = {},
        std::source_location where = std::source_location::current());

private:
    template <class I>
    friend class detail::ComponentRegistration;

    struct Prepared {
        const detail::RegistrationNode* registration;
        ParameterValues values;
    };

    template <class I, class T>
    // [[maybe_unused]]: MSVC's C4100 (/W4 /WX) fires for a parameter used only in a discarded
    // if-constexpr branch.
    static std::unique_ptr<I> construct(ObjectId owner,
                                        [[maybe_unused]] const ParameterValues& values,
                                        std::source_location where) {
        if constexpr (detail::TakesParameterValues<T>) {
            return std::make_unique<T>(
                CreationKey<ComponentRegistry>(LifetimeRegistry::instance().issue<T>(owner, where)),
                values);
        } else {
            return std::make_unique<T>(CreationKey<ComponentRegistry>(
                LifetimeRegistry::instance().issue<T>(owner, where)));
        }
    }

    // Runs every registered component's schema() and probe(caps) once (see create()).
    [[nodiscard]] static std::vector<Entry> probeComponents(const Capabilities& caps);
    [[nodiscard]] Result<Prepared> prepare(std::string_view id, const std::type_info& interface,
                                           const ParameterValues& values) const;
    [[nodiscard]] std::vector<ComponentStatus> listFor(const std::type_info& interface) const;
    // Called from a catch block: disables `id` and describes the current exception.
    [[nodiscard]] Error constructionFailed(std::string_view id);

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;         // sorted by id
    std::vector<ModuleStatus> modules_;  // immutable after construction
    Capabilities capabilities_;          // immutable after construction
    detail::ModuleHandles handles_;      // declared last: released first
};

template <class I>
template <class T>
detail::ComponentRegistration<I>::ComponentRegistration(std::type_identity<T> /*type*/) noexcept
    : RegistrationNode(
          typeid(I),
          [] {
              if constexpr (HasInterfaceKind<I>) {
                  return std::optional<Kind>(InterfaceKind<I>::value);
              } else {
                  return std::optional<Kind>();
              }
          }(),
          RegistrationHooks{.info = []() noexcept { return T::info(); },
                            .schema = []() -> const ParameterSchema& {
                                if constexpr (DeclaresSchema<T>) {
                                    return T::schema();
                                } else {
                                    return noParameters();
                                }
                            },
                            .probe = [](const Capabilities& caps) { return T::probe(caps); }}),
      create_(&ComponentRegistry::construct<I, T>) {}

template <class I>
Result<std::unique_ptr<I>> ComponentRegistry::createComponent(std::string_view id,
                                                              const ParameterValues& values,
                                                              std::source_location where) {
    DVV_TRY(Prepared prepared, prepare(id, typeid(I), values));
    // prepare() checked the interface, so the node is a ComponentRegistration<I>.
    const auto& registration =
        static_cast<const detail::ComponentRegistration<I>&>(*prepared.registration);
    try {
        return registration.create_(objectId(), prepared.values, where);
    } catch (const std::bad_alloc&) {
        // A shortage of memory is not the component's fault: it stays available.
        return makeError(Errc::OutOfMemory, std::format("component '{}': out of memory", id));
    } catch (...) {
        return std::unexpected(constructionFailed(id));
    }
}

}  // namespace dvv

// Registers component Type for interface Interface; use once at namespace scope in the component's
// .cpp, followed by a semicolon. Checks the rules of spec §9.2 and §9.4 at compile time.
// Interface and Type are type names (template arguments), which cannot be parenthesized.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define DVV_REGISTER(Interface, Type)                                                            \
    static_assert(std::is_final_v<Type>, "DVV_REGISTER: " #Type " must be final");               \
    static_assert(std::derived_from<Type, ::dvv::Managed<Type>>,                                 \
                  "DVV_REGISTER: " #Type " must derive dvv::Managed<" #Type ">");                \
    static_assert(!std::is_copy_constructible_v<Type> && !std::is_move_constructible_v<Type>,    \
                  "DVV_REGISTER: " #Type " must not be copy or move constructible");             \
    static_assert(std::has_virtual_destructor_v<Interface>,                                      \
                  "DVV_REGISTER: " #Interface " needs a virtual destructor");                    \
    static_assert(::dvv::Component<Type, Interface>,                                             \
                  "DVV_REGISTER: " #Type " must implement " #Interface                           \
                  " and provide static info() noexcept and probe()");                            \
    static_assert(std::is_constructible_v<Type, ::dvv::CreationKey<::dvv::ComponentRegistry>> || \
                      ::dvv::detail::TakesParameterValues<Type>,                                 \
                  "DVV_REGISTER: " #Type                                                         \
                  " must be constructible from "                                                 \
                  "(dvv::CreationKey<dvv::ComponentRegistry>)");                                 \
    static_assert(                                                                               \
        !::dvv::detail::DeclaresSchema<Type> || ::dvv::detail::TakesParameterValues<Type>,       \
        "DVV_REGISTER: " #Type                                                                   \
        " declares schema() but is not constructible from "                                      \
        "(dvv::CreationKey<dvv::ComponentRegistry>, const dvv::ParameterValues&)");              \
    static const ::dvv::detail::ComponentRegistration<Interface> DVV_DETAIL_CONCAT(              \
        dvv_registration_, __LINE__)(std::type_identity<Type>{})
// NOLINTEND(bugprone-macro-parentheses)
