// A component in its own static library; see linked_component.h.
#include "linked_component.h"

#include <array>
#include <expected>
#include <string_view>
#include <utility>

#include "dvv/registry/component_registry.h"

namespace dvv::test {

namespace {

class Linked final : public ILinked, public Managed<Linked> {
public:
    explicit Linked(CreationKey<ComponentRegistry> key) noexcept : Managed(std::move(key)) {}

    static ComponentInfo info() noexcept { return {.id = "test.linked", .name = "Linked"}; }
    static std::expected<void, Unavailable> probe(const Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int answer() const override { return 42; }
};
DVV_REGISTER(ILinked, Linked);

constexpr std::array<std::string_view, 1> LINKED_PRODUCES{"test.linked_ready"};

struct LinkedModule {
    static ModuleInfo info() noexcept {
        return {.id = "test.linked_module", .name = "Linked module", .produces = LINKED_PRODUCES};
    }
    static ModuleLoad load(const ParameterValues& /*options*/) {
        return {.provides = {"test.linked_ready"}};
    }
};
DVV_REGISTER_MODULE(LinkedModule);

}  // namespace

}  // namespace dvv::test
