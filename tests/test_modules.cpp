// Every load() in this binary runs every module below, also from several threads at once, so each
// test reads only its own module ids, and module state is a std::atomic or guarded by one
// std::mutex.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/lifetime.h"
#include "dvv/registry/component_registry.h"
#include "dvv/registry/module.h"

namespace {

// The values a module's load() got last, guarded by one mutex.
class RecordedValues {
public:
    void record(const dvv::ParameterValues& values) {
        const std::lock_guard lock(mutex_);
        values_ = values;
    }
    [[nodiscard]] dvv::ParameterValues last() const {
        const std::lock_guard lock(mutex_);
        return values_;
    }

private:
    mutable std::mutex mutex_;
    dvv::ParameterValues values_;
};

RecordedValues ready_values;
RecordedValues options_values;

constexpr std::array<std::string_view, 2> READY_PRODUCES{"test.ready", "test.extra"};

struct ReadyModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.ready", .name = "Ready", .produces = READY_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& options) {
        ready_values.record(options);
        return {.provides = {"test.ready"},
                .reason = "test.extra is switched off",
                .facts = {{"version", "1.2"}, {"licence", "MIT"}, {"directory", "/fake/modules"}}};
    }
};

DVV_REGISTER_MODULE(ReadyModule);

// Produces nothing; records the values it got.
struct OptionsModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.options", .name = "Options"}; }
    static dvv::ModuleLoad load(const dvv::ParameterValues& options) {
        options_values.record(options);
        return {};
    }
};

DVV_REGISTER_MODULE(OptionsModule);

std::atomic<int> counting_loads{0};
std::atomic<int> counting_releases{0};

// Produces nothing; counts its loads, and its handle's deleter counts the releases.
struct CountingModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.counting", .name = "Counting"};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        ++counting_loads;
        return {.handle = std::shared_ptr<int>(new int(0), [](const int* state) {
                    ++counting_releases;
                    delete state;
                })};
    }
};

DVV_REGISTER_MODULE(CountingModule);

constexpr std::array<std::string_view, 3> MESSY_PRODUCES{"test.m.a", "test.m.b", "test.m.c"};

// Returns its provides out of order and with a repeat.
struct MessyModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.messy", .name = "Messy", .produces = MESSY_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.m.c", "test.m.a", "test.m.c"}, .reason = "test.m.b is off"};
    }
};

DVV_REGISTER_MODULE(MessyModule);

constexpr std::array<std::string_view, 2> COMPLETE_PRODUCES{"test.c.one", "test.c.two"};

// Provides everything it produces, yet returns a reason.
struct CompleteModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.complete", .name = "Complete", .produces = COMPLETE_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.c.one", "test.c.two"}, .reason = "stray text"};
    }
};

DVV_REGISTER_MODULE(CompleteModule);

constexpr std::array<std::string_view, 1> SHARED_CAP_PRODUCES{"test.shared_cap"};

// Two modules that provide the same capability.
struct SharedOneModule {
    static dvv::ModuleInfo info() noexcept {
        return {
            .id = "test.mod.shared_cap.one", .name = "Shared one", .produces = SHARED_CAP_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.shared_cap"}};
    }
};

DVV_REGISTER_MODULE(SharedOneModule);

struct SharedTwoModule {
    static dvv::ModuleInfo info() noexcept {
        return {
            .id = "test.mod.shared_cap.two", .name = "Shared two", .produces = SHARED_CAP_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.shared_cap"}};
    }
};

DVV_REGISTER_MODULE(SharedTwoModule);

constexpr std::array<std::string_view, 3> REPEATS_PRODUCES{"test.r.a", "test.r.a", "test.r.b"};

// Its produces list names one capability twice.
struct RepeatsModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.repeats", .name = "Repeats", .produces = REPEATS_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        return {.provides = {"test.r.a"}};
    }
};

DVV_REGISTER_MODULE(RepeatsModule);

// The interface of the components that need a module's capability.
class IModuleProbe {
public:
    IModuleProbe() = default;
    IModuleProbe(const IModuleProbe&) = delete;
    IModuleProbe& operator=(const IModuleProbe&) = delete;
    IModuleProbe(IModuleProbe&&) = delete;
    IModuleProbe& operator=(IModuleProbe&&) = delete;
    virtual ~IModuleProbe() = default;
};

using Key = dvv::CreationKey<dvv::ComponentRegistry>;
using Probed = std::expected<void, dvv::Unavailable>;

class NeedsReady final : public IModuleProbe, public dvv::Managed<NeedsReady> {
public:
    explicit NeedsReady(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.needs_ready", .name = "Needs ready"};
    }
    static Probed probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"test.ready"});
    }
};
DVV_REGISTER(IModuleProbe, NeedsReady);

class NeedsSharedCap final : public IModuleProbe, public dvv::Managed<NeedsSharedCap> {
public:
    explicit NeedsSharedCap(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.needs_shared_cap", .name = "Needs shared cap"};
    }
    static Probed probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"test.shared_cap"});
    }
};
DVV_REGISTER(IModuleProbe, NeedsSharedCap);

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

TEST_CASE("load runs every module and probes the components with what they provide") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);
    CHECK(ready->provides == std::vector<std::string>{"test.ready"});
    CHECK(ready->withheld == std::vector<std::string>{"test.extra"});
    CHECK(ready->reason == "test.extra is switched off");
    CHECK(registry->capabilities().has("test.ready"));
    CHECK(registry->createComponent<IModuleProbe>("test.needs_ready").has_value());
}

TEST_CASE("create runs no module") {
    const int loads = counting_loads.load();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{"x"});
    CHECK(counting_loads.load() == loads);
    CHECK(registry->modules().empty());
    CHECK(registry->capabilities().names() == std::vector<std::string>{"x"});

    const auto statuses = registry->list<IModuleProbe>();
    const auto needs_ready =
        std::ranges::find(statuses, "test.needs_ready",
                          [](const dvv::ComponentStatus& status) { return status.info.id; });
    REQUIRE(needs_ready != statuses.end());
    CHECK_FALSE(needs_ready->available());
    CHECK(needs_ready->unavailable.value_or(dvv::Unavailable{}).capability == "test.ready");
    CHECK_FALSE(registry->createComponent<IModuleProbe>("test.needs_ready").has_value());
}

TEST_CASE("module options reach their module and only it") {
    const auto registry = dvv::ComponentRegistry::load(
        {.modules = {{"test.mod.options", {{"k", std::string("v")}}}}});
    CHECK(options_values.last() == dvv::ParameterValues{{"k", std::string("v")}});
    CHECK(ready_values.last().empty());
}

TEST_CASE("a module's provides are deduplicated and kept in produces order") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* messy = registry->module("test.mod.messy");
    REQUIRE(messy != nullptr);
    CHECK(messy->provides == std::vector<std::string>{"test.m.a", "test.m.c"});
    CHECK(messy->withheld == std::vector<std::string>{"test.m.b"});
    CHECK(messy->reason == "test.m.b is off");
}

TEST_CASE("a module that provides everything has no reason") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* complete = registry->module("test.mod.complete");
    REQUIRE(complete != nullptr);
    CHECK(complete->provides == std::vector<std::string>{"test.c.one", "test.c.two"});
    CHECK(complete->withheld.empty());
    CHECK(complete->reason.empty());
}

TEST_CASE("two modules that provide one capability") {
    const auto registry = dvv::ComponentRegistry::load();
    CHECK(std::ranges::count(registry->capabilities().names(), "test.shared_cap") == 1);
    const dvv::ModuleStatus* one = registry->module("test.mod.shared_cap.one");
    const dvv::ModuleStatus* two = registry->module("test.mod.shared_cap.two");
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK(one->provides == std::vector<std::string>{"test.shared_cap"});
    CHECK(two->provides == std::vector<std::string>{"test.shared_cap"});
    CHECK(registry->createComponent<IModuleProbe>("test.needs_shared_cap").has_value());
}

TEST_CASE("a produces list with a repeated name") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* repeats = registry->module("test.mod.repeats");
    REQUIRE(repeats != nullptr);
    CHECK(repeats->provides == std::vector<std::string>{"test.r.a"});
    CHECK(repeats->withheld == std::vector<std::string>{"test.r.b"});
}

TEST_CASE("modules are listed and found by id") {
    const auto registry = dvv::ComponentRegistry::load();
    const auto modules = registry->modules();
    CHECK(std::ranges::is_sorted(modules, {},
                                 [](const dvv::ModuleStatus& status) { return status.info.id; }));
    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);
    CHECK(ready->info.id == "test.mod.ready");
    // The pointer refers into modules(), not to a copy.
    CHECK(std::ranges::any_of(
        modules, [ready](const dvv::ModuleStatus& status) { return &status == ready; }));
    CHECK(registry->module("test.none") == nullptr);
}

TEST_CASE("module facts keep their order and are found by key") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);
    CHECK(ready->facts == std::vector<dvv::ModuleFact>{{"version", "1.2"},
                                                       {"licence", "MIT"},
                                                       {"directory", "/fake/modules"}});
    CHECK(ready->fact("version") == std::optional<std::string_view>{"1.2"});
    CHECK(ready->fact("absent") == std::nullopt);
}

TEST_CASE("load records its caller as the creation site") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const bool previous = lifetimes.detailed();
    lifetimes.setDetailed(true);
    const auto line = std::source_location::current().line() + 1;
    const auto registry = dvv::ComponentRegistry::load();
    const auto objects = lifetimes.liveObjects();
    lifetimes.setDetailed(previous);

    const auto found = std::ranges::find(objects, registry->objectId(), &dvv::LiveObjectInfo::id);
    REQUIRE(found != objects.end());
    CHECK(found->createdAt.has_value());
    const std::source_location where = found->createdAt.value_or(std::source_location());
    CHECK(std::string_view(where.file_name()).ends_with("test_modules.cpp"));
    CHECK(where.line() == line);
}
