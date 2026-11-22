#include "component.h"

#include <array>
#include <expected>
#include <string_view>
#include <utility>

#include "dvv/registry/component_registry.h"

namespace {

class Greeting final : public IGreeting, public dvv::Managed<Greeting> {
public:
    explicit Greeting(dvv::CreationKey<dvv::ComponentRegistry> key) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "consumer.greeting", .name = "Greeting"};
    }
    static std::expected<void, dvv::Unavailable> probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"consumer.ready"});
    }

    [[nodiscard]] const char* text() const override { return "dvv_registry package works"; }
};
DVV_REGISTER(IGreeting, Greeting);

constexpr std::array<std::string_view, 1> CONSUMER_PRODUCES{"consumer.ready"};

struct ConsumerModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "consumer", .name = "Consumer", .produces = CONSUMER_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues&) {
        return {.provides = {"consumer.ready"}};
    }
};
DVV_REGISTER_MODULE(ConsumerModule);

}  // namespace
