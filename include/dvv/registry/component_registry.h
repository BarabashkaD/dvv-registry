#pragma once

// Components and the registry that creates them (spec §9.1, §9.2, §9.4).
//
// A component is a final class implementing one interface I (IMetric, ISource, ...), deriving
// Managed<Self>, with static info() and probe() and a constructor taking
// CreationKey<ComponentRegistry>. One line at namespace scope in its .cpp registers it:
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

namespace dvv {

class ComponentRegistry;

// T implements interface I as a component (spec §9.2). info() must not throw: the registry needs
// the id to report anything else about the component.
template <class T, class I>
concept Component = std::derived_from<T, I> && requires(const Capabilities& caps) {
    { T::info() } noexcept -> std::same_as<ComponentInfo>;
    { T::probe(caps) } -> std::same_as<std::expected<void, Unavailable>>;
};

namespace detail {

// The static functions of one registered component.
struct RegistrationHooks {
    ComponentInfo (*info)() noexcept;
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
    RegistrationNode(const std::type_info& interface, RegistrationHooks hooks) noexcept;
    ~RegistrationNode() = default;

private:
    friend class dvv::ComponentRegistry;

    // Constant-initialised, so it is valid before any registration runs.
    static constinit RegistrationNode* list_head;

    const std::type_info* interface_;
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
    using CreateFunction = std::unique_ptr<I> (*)(ObjectId owner, std::source_location where);

    CreateFunction create_;
};

}  // namespace detail

// What the registry knows about one component.
struct ComponentStatus {
    ComponentInfo info;
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
    // Runs every registered component's probe(caps) once. A component whose probe() throws is
    // unavailable with an "Internal: ..." reason, and so is every component whose id is registered
    // more than once.
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

    // Creates component `id`. Errc::Unsupported for an unknown or unavailable id or one with
    // another interface. std::bad_alloc from the constructor becomes Errc::OutOfMemory and leaves
    // the component available; any other constructor exception becomes Errc::Internal and makes the
    // component unavailable for this registry's lifetime. `where` is the creation site that leak
    // reports show: the caller.
    template <class I>
    [[nodiscard]] Result<std::unique_ptr<I>> createComponent(
        std::string_view id, std::source_location where = std::source_location::current());

private:
    template <class I>
    friend class detail::ComponentRegistration;

    template <class I, class T>
    static std::unique_ptr<I> construct(ObjectId owner, std::source_location where) {
        return std::make_unique<T>(
            CreationKey<ComponentRegistry>(LifetimeRegistry::instance().issue<T>(owner, where)));
    }

    // Runs every registered component's probe(caps) once (see create()).
    [[nodiscard]] static std::vector<Entry> probeComponents(const Capabilities& caps);
    // The registration node of `id` when it implements `interface` and is available.
    [[nodiscard]] Result<const detail::RegistrationNode*> prepare(
        std::string_view id, const std::type_info& interface) const;
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
          RegistrationHooks{.info = []() noexcept { return T::info(); },
                            .probe = [](const Capabilities& caps) { return T::probe(caps); }}),
      create_(&ComponentRegistry::construct<I, T>) {}

template <class I>
Result<std::unique_ptr<I>> ComponentRegistry::createComponent(std::string_view id,
                                                              std::source_location where) {
    DVV_TRY(const detail::RegistrationNode* node, prepare(id, typeid(I)));
    // prepare() checked the interface, so the node is a ComponentRegistration<I>.
    const auto& registration = static_cast<const detail::ComponentRegistration<I>&>(*node);
    try {
        return registration.create_(objectId(), where);
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
#define DVV_REGISTER(Interface, Type)                                                          \
    static_assert(std::is_final_v<Type>, "DVV_REGISTER: " #Type " must be final");             \
    static_assert(std::derived_from<Type, ::dvv::Managed<Type>>,                               \
                  "DVV_REGISTER: " #Type " must derive dvv::Managed<" #Type ">");              \
    static_assert(!std::is_copy_constructible_v<Type> && !std::is_move_constructible_v<Type>,  \
                  "DVV_REGISTER: " #Type " must not be copy or move constructible");           \
    static_assert(std::has_virtual_destructor_v<Interface>,                                    \
                  "DVV_REGISTER: " #Interface " needs a virtual destructor");                  \
    static_assert(::dvv::Component<Type, Interface>,                                           \
                  "DVV_REGISTER: " #Type " must implement " #Interface                         \
                  " and provide static info() noexcept and probe()");                          \
    static_assert(std::is_constructible_v<Type, ::dvv::CreationKey<::dvv::ComponentRegistry>>, \
                  "DVV_REGISTER: " #Type                                                       \
                  " must be constructible from (dvv::CreationKey<dvv::ComponentRegistry>)");   \
    static const ::dvv::detail::ComponentRegistration<Interface> DVV_DETAIL_CONCAT(            \
        dvv_registration_, __LINE__)(std::type_identity<Type>{})
// NOLINTEND(bugprone-macro-parentheses)
