#pragma once

// Components and the registry that creates them (spec §9.1, §9.2, §9.4).
//
// A component is a final class implementing one interface I (IMetric, ISource, ...), deriving
// Managed<Self>, with static info(), schema() and probe() and a constructor taking
// (CreationKey<ComponentRegistry>, const ParameterValues&). One line at namespace scope in its .cpp
// registers it:
//
//     DVV_REGISTER(IMetric, PsnrMetric);
//
// The line defines a static descriptor that links itself into a process-wide list during static
// initialisation (no allocation). ComponentRegistry reads that list; it is the only creator of
// components. Component libraries are linked with dvv_link_components() (WHOLE_ARCHIVE), so the
// descriptors survive linking.

#include <concepts>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_info.h"
#include "dvv/registry/parameters.h"

namespace dvv {

// T implements interface I as a component (spec §9.2). info() must not throw: the registry needs
// the id to report anything else about the component.
template <class T, class I>
concept Component = std::derived_from<T, I> && requires(const Capabilities& caps) {
    { T::info() } noexcept -> std::same_as<ComponentInfo>;
    { T::schema() } -> std::same_as<const ParameterSchema&>;
    { T::probe(caps) } -> std::same_as<std::expected<void, Unavailable>>;
};

class ComponentRegistry;

namespace detail {

// One registered component; a static object created by DVV_REGISTER. The constructor links it
// into the process-wide list, so registration happens during static initialisation, before main.
class RegistrationNode {
public:
    RegistrationNode(const RegistrationNode&) = delete;
    RegistrationNode& operator=(const RegistrationNode&) = delete;
    RegistrationNode(RegistrationNode&&) = delete;
    RegistrationNode& operator=(RegistrationNode&&) = delete;

protected:
    using InfoFunction = ComponentInfo (*)() noexcept;
    using SchemaFunction = const ParameterSchema& (*)();
    using ProbeFunction = std::expected<void, Unavailable> (*)(const Capabilities&);

    RegistrationNode(const std::type_info& interface, std::optional<Kind> interface_kind,
                     InfoFunction info, SchemaFunction schema, ProbeFunction probe) noexcept;
    ~RegistrationNode() = default;

private:
    friend class dvv::ComponentRegistry;

    // Constant-initialised, so it is valid before any registration runs.
    static constinit RegistrationNode* list_head;

    const std::type_info* interface_;
    std::optional<Kind> interface_kind_;  // InterfaceKind<I>, when I specialises it
    InfoFunction info_;
    SchemaFunction schema_;
    ProbeFunction probe_;
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
    const ParameterSchema* schema = nullptr;  // null when schema() threw
    // Empty when the component can be created.
    std::optional<Unavailable> unavailable = std::nullopt;

    [[nodiscard]] bool available() const noexcept { return !unavailable.has_value(); }
};

// The creation authority of every component (spec §9.4). Thread-safe.
class ComponentRegistry final : public Managed<ComponentRegistry> {
    struct Entry {
        const detail::RegistrationNode* registration;
        ComponentStatus status;
    };

public:
    // Runs every registered component's schema() and probe(caps) once. A component whose schema()
    // or probe() throws is unavailable with an "Internal: ..." reason, and so is every component
    // whose id is registered more than once. A component whose info().kind differs from
    // InterfaceKind<I> of its interface is unavailable without being probed.
    [[nodiscard]] static std::unique_ptr<ComponentRegistry> create(const Capabilities& caps);

    // For create(caps) only (Entry is private).
    ComponentRegistry(CreationKey<ComponentRegistry> key, std::vector<Entry> entries) noexcept;

    // Components sorted by id: all of them, or those of one kind.
    [[nodiscard]] std::vector<ComponentStatus> list() const;
    [[nodiscard]] std::vector<ComponentStatus> list(Kind kind) const;

    // Creates component `id` with `values` validated against its schema and completed with the
    // defaults. Errc::Unsupported for an unknown or unavailable id or one with another interface,
    // Errc::Format for invalid values. std::bad_alloc from the constructor becomes
    // Errc::OutOfMemory and leaves the component available; any other constructor exception becomes
    // Errc::Internal and makes the component unavailable for this registry's lifetime. `where` is
    // the creation site that leak reports show: the caller.
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
    static std::unique_ptr<I> construct(ObjectId owner, const ParameterValues& values,
                                        std::source_location where) {
        return std::make_unique<T>(
            CreationKey<ComponentRegistry>(LifetimeRegistry::instance().issue<T>(owner, where)),
            values);
    }

    [[nodiscard]] Result<Prepared> prepare(std::string_view id, const std::type_info& interface,
                                           const ParameterValues& values) const;
    // Called from a catch block: disables `id` and describes the current exception.
    [[nodiscard]] Error constructionFailed(std::string_view id);

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;  // sorted by id
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
          []() noexcept { return T::info(); },
          []() -> const ParameterSchema& { return T::schema(); },
          [](const Capabilities& caps) { return T::probe(caps); }),
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
#define DVV_REGISTER(Interface, Type)                                                         \
    static_assert(std::is_final_v<Type>, "DVV_REGISTER: " #Type " must be final");            \
    static_assert(std::derived_from<Type, ::dvv::Managed<Type>>,                              \
                  "DVV_REGISTER: " #Type " must derive dvv::Managed<" #Type ">");             \
    static_assert(!std::is_copy_constructible_v<Type> && !std::is_move_constructible_v<Type>, \
                  "DVV_REGISTER: " #Type " must not be copy or move constructible");          \
    static_assert(std::has_virtual_destructor_v<Interface>,                                   \
                  "DVV_REGISTER: " #Interface " needs a virtual destructor");                 \
    static_assert(::dvv::Component<Type, Interface>,                                          \
                  "DVV_REGISTER: " #Type " must implement " #Interface                        \
                  " and provide static info() noexcept, schema() and probe()");               \
    static_assert(std::is_constructible_v<Type, ::dvv::CreationKey<::dvv::ComponentRegistry>, \
                                          const ::dvv::ParameterValues&>,                     \
                  "DVV_REGISTER: " #Type                                                      \
                  " must be constructible from "                                              \
                  "(dvv::CreationKey<dvv::ComponentRegistry>, const dvv::ParameterValues&)"); \
    static const ::dvv::detail::ComponentRegistration<Interface> DVV_DETAIL_CONCAT(           \
        dvv_registration_, __LINE__)(std::type_identity<Type>{})
// NOLINTEND(bugprone-macro-parentheses)
