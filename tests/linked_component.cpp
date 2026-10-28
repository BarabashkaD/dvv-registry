// A component in its own static library; see linked_component.h.
#include "linked_component.h"

#include <expected>
#include <utility>

#include "dvv/registry/component_registry.h"

namespace dvv::test {

namespace {

class Linked final : public ILinked, public Managed<Linked> {
public:
    Linked(CreationKey<ComponentRegistry> key, const ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static ComponentInfo info() noexcept {
        return {.id = "test.linked", .name = "Linked", .kind = Kind::Analyser};
    }
    static const ParameterSchema& schema() {
        static const ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static std::expected<void, Unavailable> probe(const Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int answer() const override { return 42; }
};
DVV_REGISTER(ILinked, Linked);

}  // namespace

}  // namespace dvv::test
