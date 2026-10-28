// Compile-fail cases for components (spec §9.4). DVV_COMPILE_FAIL_CASE selects one:
//   0  control: a registered component created through the registry compiles
//   1  constructing a CreationKey<ComponentRegistry> outside the registry
//   2  constructing a registered component without a key
//   3  registering a class that is not final
//   4  registering a class whose constructor takes another authority's key
//   5  registering a class without probe()
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
#endif

}  // namespace

int main() {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    auto widget = registry->createComponent<IWidget>("compile_fail.widget");
#if DVV_COMPILE_FAIL_CASE == 1
    dvv::CreationKey<dvv::ComponentRegistry> forged(
        dvv::LifetimeRegistry::instance().issue<Widget>());
#elif DVV_COMPILE_FAIL_CASE == 2
    Widget direct(dvv::ParameterValues{});
#endif
    return widget.has_value() ? 0 : 1;
}
