// Compile-fail cases for components (spec §9.4). DVV_COMPILE_FAIL_CASE selects one:
//   0  control: registered components created through the registry compile, also a component
//      without schema() and with a one-argument constructor, and a registered module
//   1  constructing a CreationKey<ComponentRegistry> outside the registry
//   2  constructing a registered component without a key
//   3  registering a class that is not final
//   4  registering a class whose constructor takes another authority's key
//   5  registering a class without probe()
//   6  registering a module without load()
//   7  registering a class with schema() and only the one-argument constructor
//   8  registering a class without a constructor the registry can call
#include <expected>
#include <memory>
#include <utility>

#include "dvv/base/lifetime.h"
#include "dvv/base/testing/test_authority.h"
#include "dvv/registry/component_registry.h"

namespace {

class IWidget {
public:
    IWidget() = default;
    IWidget(const IWidget&) = delete;
    IWidget& operator=(const IWidget&) = delete;
    IWidget(IWidget&&) = delete;
    IWidget& operator=(IWidget&&) = delete;
    virtual ~IWidget() = default;
};

using Probed = std::expected<void, dvv::Unavailable>;

// The static parts every widget shares.
struct WidgetStatics {
    static dvv::ComponentInfo info() noexcept {
        return {.id = "compile_fail.widget", .name = "Widget", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }
};

class Widget final : public IWidget, public WidgetStatics, public dvv::Managed<Widget> {
public:
    Widget(dvv::CreationKey<dvv::ComponentRegistry> key,
           const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}
};
DVV_REGISTER(IWidget, Widget);

// The final form: no schema(), a one-argument constructor.
class Gadget final : public IWidget, public dvv::Managed<Gadget> {
public:
    explicit Gadget(dvv::CreationKey<dvv::ComponentRegistry> key) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "compile_fail.gadget", .name = "Gadget"};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }
};
DVV_REGISTER(IWidget, Gadget);

// A module that produces nothing; registered in the control.
struct WidgetModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "compile_fail.widget_module", .name = "Widget module"};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) { return {}; }
};
DVV_REGISTER_MODULE(WidgetModule);

#if DVV_COMPILE_FAIL_CASE == 3
class NotFinal : public IWidget, public WidgetStatics, public dvv::Managed<NotFinal> {
public:
    NotFinal(dvv::CreationKey<dvv::ComponentRegistry> key,
             const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}
};
DVV_REGISTER(IWidget, NotFinal);
#elif DVV_COMPILE_FAIL_CASE == 4
class WrongKey final : public IWidget, public WidgetStatics, public dvv::Managed<WrongKey> {
public:
    WrongKey(dvv::CreationKey<dvv::test::TestAuthority> key,
             const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}
};
DVV_REGISTER(IWidget, WrongKey);
#elif DVV_COMPILE_FAIL_CASE == 5
class NoProbe final : public IWidget, public dvv::Managed<NoProbe> {
public:
    NoProbe(dvv::CreationKey<dvv::ComponentRegistry> key,
            const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}
    static dvv::ComponentInfo info() noexcept { return WidgetStatics::info(); }
    static const dvv::ParameterSchema& schema() { return WidgetStatics::schema(); }
};
DVV_REGISTER(IWidget, NoProbe);
#elif DVV_COMPILE_FAIL_CASE == 6
// Only info(): a module needs load() too.
struct NoLoadModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "compile_fail.no_load_module", .name = "No load module"};
    }
};
DVV_REGISTER_MODULE(NoLoadModule);
#elif DVV_COMPILE_FAIL_CASE == 7
class SchemaOneArg final : public IWidget, public WidgetStatics, public dvv::Managed<SchemaOneArg> {
public:
    explicit SchemaOneArg(dvv::CreationKey<dvv::ComponentRegistry> key) noexcept
        : Managed(std::move(key)) {}
};
DVV_REGISTER(IWidget, SchemaOneArg);
#elif DVV_COMPILE_FAIL_CASE == 8
class IntOnly final : public IWidget, public dvv::Managed<IntOnly> {
public:
    // No body: Managed<IntOnly> can only be built from the registry's key, so a defined (int)
    // constructor would add a second, unrelated error. The file fails to compile before it is used.
    explicit IntOnly(int);

    static dvv::ComponentInfo info() noexcept {
        return {.id = "compile_fail.int_only", .name = "Int only"};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }
};
DVV_REGISTER(IWidget, IntOnly);
#endif

}  // namespace

int main() {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    auto widget = registry->createComponent<IWidget>("compile_fail.widget");
    auto gadget = registry->createComponent<IWidget>("compile_fail.gadget");
#if DVV_COMPILE_FAIL_CASE == 1
    dvv::CreationKey<dvv::ComponentRegistry> forged(
        dvv::LifetimeRegistry::instance().issue<Widget>());
#elif DVV_COMPILE_FAIL_CASE == 2
    Widget direct(dvv::ParameterValues{});
#endif
    return widget.has_value() && gadget.has_value() ? 0 : 1;
}
