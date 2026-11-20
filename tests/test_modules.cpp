// Every load() in this binary runs every module below, also from several threads at once, so each
// test reads only its own module ids, and module state is a std::atomic or guarded by one
// std::mutex.

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/registry/component_registry.h"
#include "dvv/registry/module.h"

namespace {

constexpr std::array<std::string_view, 2> READY_PRODUCES{"test.ready", "test.extra"};

struct ReadyModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.ready", .name = "Ready", .produces = READY_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.ready"},
                .reason = "test.extra is switched off",
                .facts = {{"version", "1.2"},
                          {"licence", "MIT"},
                          {"directory", "/fake/modules"}}};
    }
};

DVV_REGISTER_MODULE(ReadyModule);

// Has info() but no load(): not a module.
struct NoLoad {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.no_load"}; }
};

// A non-null handle whose release appends `name` to `log`.
std::shared_ptr<void> tracked(std::string name, std::vector<std::string>& log) {
    return std::shared_ptr<std::string>(new std::string(std::move(name)),
                                        [&log](const std::string* released) {
                                            log.push_back(*released);
                                            delete released;
                                        });
}

template <class S>
concept FactOnRvalue = requires(S&& s) { std::move(s).fact("version"); };

}  // namespace

TEST_CASE("a module registered with DVV_REGISTER_MODULE satisfies Module") {
    static_assert(dvv::Module<ReadyModule>);
    static_assert(!dvv::Module<NoLoad>);
}

TEST_CASE("a module status finds the first fact with a key") {
    const dvv::ModuleStatus status{.facts = {{"version", "1.2"}, {"version", "2"}}};
    CHECK(status.fact("version") == std::optional<std::string_view>{"1.2"});
    CHECK(status.fact("absent") == std::nullopt);
    static_assert(!FactOnRvalue<dvv::ModuleStatus>);
}

TEST_CASE("a module status is available when it provides a capability") {
    CHECK_FALSE(dvv::ModuleStatus{}.available());
    CHECK(dvv::ModuleStatus{.provides = {"x"}}.available());
}

TEST_CASE("ModuleHandles releases in reverse order") {
    std::vector<std::string> log;
    {
        auto source = std::make_unique<dvv::detail::ModuleHandles>();
        source->add(tracked("a", log));
        source->add(tracked("b", log));
        source->add(tracked("c", log));
        source->add(nullptr);
        {
            dvv::detail::ModuleHandles moved(std::move(*source));
            source.reset();
            CHECK(log.empty());  // the moved-from instance released nothing
        }
        CHECK(log == std::vector<std::string>{"c", "b", "a"});  // moved-to releases all, in reverse
    }
    CHECK(log.size() == 3);  // nothing released twice
}
