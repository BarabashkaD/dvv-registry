#include "component.h"

#include <expected>
#include <utility>

#include "dvv/registry/component_registry.h"

namespace {

class Greeting final : public IGreeting, public dvv::Managed<Greeting> {
public:
    Greeting(dvv::CreationKey<dvv::ComponentRegistry> key,
             const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "consumer.greeting", .name = "Greeting", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static std::expected<void, dvv::Unavailable> probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"gpu.compute"});
    }

    [[nodiscard]] const char* text() const override { return "dvv_registry package works"; }
};
DVV_REGISTER(IGreeting, Greeting);

}  // namespace
