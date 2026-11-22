// Every load() in this binary runs every module below, also from several threads at once, so each
// test reads only its own module ids, and module state is a std::atomic or guarded by a
// std::mutex.

#include "test_modules.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/log.h"
#include "dvv/registry/component_registry.h"
#include "dvv/registry/module.h"

namespace {

// The values a module's load() got last, guarded by one mutex.
class RecordedValues {
public:
    void record(const dvv::ModuleValues& values) {
        const std::lock_guard lock(mutex_);
        values_ = values;
    }
    [[nodiscard]] dvv::ModuleValues last() const {
        const std::lock_guard lock(mutex_);
        return values_;
    }

private:
    mutable std::mutex mutex_;
    dvv::ModuleValues values_;
};

RecordedValues ready_values;
RecordedValues options_values;

constexpr std::array<std::string_view, 2> READY_PRODUCES{"test.ready", "test.extra"};

struct ReadyModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.ready", .name = "Ready", .produces = READY_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& options) {
        ready_values.record(options);
        return {
            .provides = {"test.ready"},
            .reason = "test.extra is switched off",
            .properties = {{"version", "1.2"}, {"licence", "MIT"}, {"directory", "/fake/modules"}}};
    }
};

DVV_REGISTER_MODULE(ReadyModule);

// Produces nothing; records the values it got.
struct OptionsModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.options", .name = "Options"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& options) {
        options_values.record(options);
        return {};
    }
};

DVV_REGISTER_MODULE(OptionsModule);

std::atomic<int> counting_loads{0};
std::atomic<int> counting_releases{0};

// A module state whose end counts a release.
class CountedState {
public:
    CountedState() = default;
    CountedState(const CountedState&) = delete;
    CountedState& operator=(const CountedState&) = delete;
    CountedState(CountedState&&) = delete;
    CountedState& operator=(CountedState&&) = delete;
    ~CountedState() { ++counting_releases; }
};

// Produces nothing; counts its loads, and the end of its state counts the releases.
struct CountingModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.counting", .name = "Counting"};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        ++counting_loads;
        return {.handle = std::make_shared<CountedState>()};
    }
};

DVV_REGISTER_MODULE(CountingModule);

constexpr std::array<std::string_view, 3> MESSY_PRODUCES{"test.m.a", "test.m.b", "test.m.c"};

// Returns its provides out of order and with a repeat.
struct MessyModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.messy", .name = "Messy", .produces = MESSY_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
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
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
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
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return {.provides = {"test.shared_cap"}};
    }
};

DVV_REGISTER_MODULE(SharedOneModule);

struct SharedTwoModule {
    static dvv::ModuleInfo info() noexcept {
        return {
            .id = "test.mod.shared_cap.two", .name = "Shared two", .produces = SHARED_CAP_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
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
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return {.provides = {"test.r.a"}};
    }
};

DVV_REGISTER_MODULE(RepeatsModule);

constexpr std::array<std::string_view, 1> DUP_PRODUCES{"test.dup"};

// test_modules_duplicate.cpp registers a second module with this id.
struct DuplicateModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.dup", .name = "Duplicate (first)", .produces = DUP_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        ++dvv::test::duplicate_module_loads;
        return {.provides = {"test.dup"}};
    }
};

DVV_REGISTER_MODULE(DuplicateModule);

constexpr std::array<std::string_view, 1> THROWS_PRODUCES{"test.thrown"};

// Its exception's text holds a folder, which must never reach the log.
struct ThrowsModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.throws", .name = "Throws", .produces = THROWS_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        throw std::runtime_error("cannot open /fake/modules/lib");
    }
};

DVV_REGISTER_MODULE(ThrowsModule);

// Produces nothing; throws something that is not a std::exception.
struct ThrowsOtherModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.throws_other", .name = "Throws other"};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) { throw 42; }
};

DVV_REGISTER_MODULE(ThrowsOtherModule);

// Produces nothing; throws from a failed Result<T>::value(), so the reason is Error::describe().
struct ResultThrowsModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.result_throws", .name = "Result throws"};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return dvv::Result<dvv::ModuleLoad>(dvv::makeError(dvv::Errc::Format, "bad option"))
            .value();
    }
};
DVV_REGISTER_MODULE(ResultThrowsModule);

constexpr std::array<std::string_view, 1> UNDECLARED_PRODUCES{"test.declared"};

// Provides a capability its info() does not list.
struct UndeclaredModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.undeclared", .name = "Undeclared", .produces = UNDECLARED_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/);
};

DVV_REGISTER_MODULE(UndeclaredModule);

// What the module-state tests observe of their modules' handles. The latest load() of each module
// overwrites its entry.
struct ModuleStates {
    std::vector<std::string> released;  // names the order modules' deleters append
    std::weak_ptr<void> weak;           // test.mod.weak's handle
    std::shared_ptr<void> sharedCopy;   // test.mod.shared's own copy of its handle
    std::weak_ptr<void> shared;         // test.mod.shared's handle
    std::weak_ptr<void> idle;           // test.mod.idle's handle
    std::weak_ptr<void> undeclared;     // test.mod.undeclared's handle
};

std::mutex module_states_mutex;
ModuleStates module_states;

// Runs `use` on the module states while holding their mutex.
template <class Use>
decltype(auto) withModuleStates(Use&& use) {
    const std::lock_guard lock(module_states_mutex);
    return std::forward<Use>(use)(module_states);
}

// Set when an ordered handle's release could not be appended to module_states.released.
std::atomic<bool> release_unrecorded{false};

// A module state whose end appends its name to module_states.released. A destructor must not
// throw, so an end it cannot append sets release_unrecorded instead.
class OrderedState {
public:
    explicit OrderedState(std::string name) : name_(std::move(name)) {}
    OrderedState(const OrderedState&) = delete;
    OrderedState& operator=(const OrderedState&) = delete;
    OrderedState(OrderedState&&) = delete;
    OrderedState& operator=(OrderedState&&) = delete;
    ~OrderedState() {
        try {
            withModuleStates([this](ModuleStates& states) { states.released.push_back(name_); });
        } catch (...) {
            release_unrecorded = true;
        }
    }

private:
    std::string name_;
};

std::shared_ptr<void> ordered(std::string name) {
    return std::make_shared<OrderedState>(std::move(name));
}

dvv::ModuleLoad UndeclaredModule::load(const dvv::ModuleValues& /*options*/) {
    auto handle = std::make_shared<int>(0);
    withModuleStates([&handle](ModuleStates& states) { states.undeclared = handle; });
    return {.provides = {"test.declared", "test.undeclared"},
            .properties = {{"directory", "/fake/modules/undeclared"}},
            .handle = std::move(handle)};
}

// Produce nothing; their handles log their release.
struct OrderAModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.order.a", .name = "Order A"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return {.handle = ordered("a")};
    }
};

DVV_REGISTER_MODULE(OrderAModule);

struct OrderBModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.order.b", .name = "Order B"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return {.handle = ordered("b")};
    }
};

DVV_REGISTER_MODULE(OrderBModule);

// Keeps only a weak_ptr to its handle.
struct WeakModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.weak", .name = "Weak"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        auto handle = std::make_shared<int>(0);
        withModuleStates([&handle](ModuleStates& states) { states.weak = handle; });
        return {.handle = std::move(handle)};
    }
};

DVV_REGISTER_MODULE(WeakModule);

// Keeps its own shared_ptr copy of its handle, as for components that use its state.
struct SharedModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.shared", .name = "Shared"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        auto handle = std::make_shared<int>(0);
        withModuleStates([&handle](ModuleStates& states) {
            states.sharedCopy = handle;
            states.shared = handle;
        });
        return {.handle = std::move(handle)};
    }
};

DVV_REGISTER_MODULE(SharedModule);

// Produces nothing and provides nothing, yet keeps state.
struct IdleModule {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.idle", .name = "Idle"}; }
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        auto handle = std::make_shared<int>(0);
        withModuleStates([&handle](ModuleStates& states) { states.idle = handle; });
        return {.handle = std::move(handle)};
    }
};

DVV_REGISTER_MODULE(IdleModule);

// Whether the handle `which` selects has been released.
bool expired(std::weak_ptr<void> ModuleStates::* which) {
    return withModuleStates(
        [which](const ModuleStates& states) { return (states.*which).expired(); });
}

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

class NeedsThrown final : public IModuleProbe, public dvv::Managed<NeedsThrown> {
public:
    explicit NeedsThrown(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.needs_thrown", .name = "Needs thrown"};
    }
    static Probed probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"test.thrown"});
    }
};
DVV_REGISTER(IModuleProbe, NeedsThrown);

// The status of component `id` among `statuses`; nullopt when it is not there.
std::optional<dvv::ComponentStatus> findComponent(const std::vector<dvv::ComponentStatus>& statuses,
                                                  std::string_view id) {
    const auto found = std::ranges::find(
        statuses, id, [](const dvv::ComponentStatus& status) { return status.info.id; });
    return found != statuses.end() ? std::optional(*found) : std::nullopt;
}

// The statuses among `modules` with `id`, in order.
std::vector<const dvv::ModuleStatus*> findModules(std::span<const dvv::ModuleStatus> modules,
                                                  std::string_view id) {
    std::vector<const dvv::ModuleStatus*> found;
    for (const dvv::ModuleStatus& status : modules) {
        if (status.info.id == id) {
            found.push_back(&status);
        }
    }
    return found;
}

// Captures the log while it lives; the destructor removes every sink.
class CapturedLog {
public:
    CapturedLog() { dvv::Logger::instance().addSink(sink_); }
    CapturedLog(const CapturedLog&) = delete;
    CapturedLog& operator=(const CapturedLog&) = delete;
    CapturedLog(CapturedLog&&) = delete;
    CapturedLog& operator=(CapturedLog&&) = delete;
    ~CapturedLog() { dvv::Logger::instance().clearSinks(); }

    // The number of lines that hold `text`.
    [[nodiscard]] std::size_t count(std::string_view text) const {
        const auto lines = sink_->lines();
        return static_cast<std::size_t>(std::ranges::count_if(
            lines, [text](const std::string& line) { return line.contains(text); }));
    }
    [[nodiscard]] bool holds(std::string_view text) const { return count(text) > 0; }

private:
    std::shared_ptr<dvv::MemorySink> sink_ = std::make_shared<dvv::MemorySink>();
};

// Has info() but no load(): not a module.
struct NoLoad {
    static dvv::ModuleInfo info() noexcept { return {.id = "test.mod.no_load"}; }
};

// A state whose end appends its name to `log`. A destructor must not throw, so an end it cannot
// append sets `unrecorded` instead.
class TrackedState {
public:
    TrackedState(std::string name, std::vector<std::string>& log, bool& unrecorded)
        : name_(std::move(name)), log_(&log), unrecorded_(&unrecorded) {}
    TrackedState(const TrackedState&) = delete;
    TrackedState& operator=(const TrackedState&) = delete;
    TrackedState(TrackedState&&) = delete;
    TrackedState& operator=(TrackedState&&) = delete;
    ~TrackedState() {
        try {
            log_->push_back(name_);
        } catch (...) {
            *unrecorded_ = true;
        }
    }

private:
    std::string name_;
    std::vector<std::string>* log_;
    bool* unrecorded_;
};

// A non-null handle whose release appends `name` to `log`.
std::shared_ptr<void> tracked(std::string name, std::vector<std::string>& log, bool& unrecorded) {
    return std::make_shared<TrackedState>(std::move(name), log, unrecorded);
}

template <class S>
concept PropertyOnRvalue = requires(S&& s) { std::move(s).property("version"); };

// An option value is text: only text builds module options.
template <class V>
concept BuildsModuleOptions =
    requires(V value) { dvv::ModuleOptions{.modules = {{"m", {{"k", value}}}}}; };

}  // namespace

TEST_CASE("a module registered with DVV_REGISTER_MODULE satisfies Module") {
    static_assert(std::same_as<dvv::ModuleValues, std::map<std::string, std::string, std::less<>>>);
    static_assert(BuildsModuleOptions<const char*>);
    static_assert(BuildsModuleOptions<std::string>);
    static_assert(!BuildsModuleOptions<bool>);
    static_assert(!BuildsModuleOptions<std::int64_t>);
    static_assert(!BuildsModuleOptions<double>);
    static_assert(dvv::Module<ReadyModule>);
    static_assert(!dvv::Module<NoLoad>);
}

TEST_CASE("a module status finds the first property with a key") {
    const dvv::ModuleStatus status{.properties = {{"version", "1.2"}, {"version", "2"}}};
    CHECK(status.property("version") == std::optional<std::string_view>{"1.2"});
    CHECK(status.property("absent") == std::nullopt);
    static_assert(!PropertyOnRvalue<dvv::ModuleStatus>);
}

TEST_CASE("a module status is available when it provides a capability") {
    CHECK_FALSE(dvv::ModuleStatus{}.available());
    CHECK(dvv::ModuleStatus{.provides = {"x"}}.available());
}

TEST_CASE("ModuleHandles releases in reverse order") {
    std::vector<std::string> log;
    bool unrecorded = false;
    {
        auto source = std::make_unique<dvv::detail::ModuleHandles>();
        source->add(tracked("a", log, unrecorded));
        source->add(tracked("b", log, unrecorded));
        source->add(tracked("c", log, unrecorded));
        source->add(nullptr);
        {
            dvv::detail::ModuleHandles moved(std::move(*source));
            source.reset();
            CHECK(log.empty());  // the moved-from instance released nothing
        }
        CHECK(log == std::vector<std::string>{"c", "b", "a"});  // moved-to releases all, in reverse
    }
    CHECK(log.size() == 3);  // nothing released twice
    CHECK_FALSE(unrecorded);
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

// last() holds this test's load() only because doctest runs the test cases one after another.
TEST_CASE("module options reach their module and only it") {
    const auto registry =
        dvv::ComponentRegistry::load({.modules = {{"test.mod.options", {{"k", "v"}}}}});
    CHECK(options_values.last() == dvv::ModuleValues{{"k", "v"}});
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

TEST_CASE("module properties keep their order and are found by key") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);
    CHECK(ready->properties == std::vector<dvv::ModuleProperty>{{"version", "1.2"},
                                                                {"licence", "MIT"},
                                                                {"directory", "/fake/modules"}});
    CHECK(ready->property("version") == std::optional<std::string_view>{"1.2"});
    CHECK(ready->property("absent") == std::nullopt);
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

TEST_CASE("options for an unknown module are ignored with a warning") {
    const CapturedLog log;
    const auto registry = dvv::ComponentRegistry::load({.modules = {{"test.nope", {}}}});
    CHECK(registry != nullptr);
    CHECK(log.holds("module options for 'test.nope': no such module"));
}

TEST_CASE("a throwing load provides nothing and leaves the rest working") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* throws = registry->module("test.mod.throws");
    REQUIRE(throws != nullptr);
    CHECK(throws->reason.starts_with("Internal: load() threw: "));
    CHECK(throws->reason == "Internal: load() threw: cannot open /fake/modules/lib");
    CHECK(throws->provides.empty());
    CHECK(throws->withheld == std::vector<std::string>{"test.thrown"});
    CHECK_FALSE(registry->capabilities().has("test.thrown"));

    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);
    CHECK(ready->provides == std::vector<std::string>{"test.ready"});

    const auto needs_thrown = findComponent(registry->list<IModuleProbe>(), "test.needs_thrown");
    REQUIRE(needs_thrown.has_value());
    CHECK_FALSE(needs_thrown.value_or(dvv::ComponentStatus{}).available());
    CHECK(needs_thrown.value_or(dvv::ComponentStatus{})
              .unavailable.value_or(dvv::Unavailable{})
              .capability == "test.thrown");
}

TEST_CASE("a load that throws a non-exception provides nothing") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* throws_other = registry->module("test.mod.throws_other");
    REQUIRE(throws_other != nullptr);
    CHECK(throws_other->reason == "Internal: load() threw: unknown exception");
    CHECK(throws_other->provides.empty());
    CHECK(throws_other->withheld.empty());
}

TEST_CASE("a load that throws from a failed Result names the error") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* result_throws = registry->module("test.mod.result_throws");
    REQUIRE(result_throws != nullptr);
    CHECK(result_throws->reason.starts_with(
        "Internal: load() threw: Format: bad option [test_modules.cpp:"));
    CHECK(result_throws->provides.empty());
    CHECK(result_throws->withheld.empty());
}

TEST_CASE("a duplicated module id is never loaded") {
    const int loads = dvv::test::duplicate_module_loads.load();
    const auto registry = dvv::ComponentRegistry::load();
    CHECK(dvv::test::duplicate_module_loads.load() == loads);
    const auto copies = findModules(registry->modules(), "test.mod.dup");
    REQUIRE(copies.size() == 2);
    for (const dvv::ModuleStatus* copy : copies) {
        CHECK(copy->reason == "Internal: duplicate module id: test.mod.dup");
        CHECK(copy->provides.empty());
        CHECK(copy->withheld == std::vector<std::string>{"test.dup"});
    }
    CHECK_FALSE(registry->capabilities().has("test.dup"));
}

TEST_CASE("a duplicated module id is found once and its options raise no warning") {
    const CapturedLog log;
    const int loads = dvv::test::duplicate_module_loads.load();
    const auto registry = dvv::ComponentRegistry::load({.modules = {{"test.mod.dup", {}}}});
    CHECK(dvv::test::duplicate_module_loads.load() == loads);
    const auto copies = findModules(registry->modules(), "test.mod.dup");
    REQUIRE_FALSE(copies.empty());
    CHECK(registry->module("test.mod.dup") == copies.front());
    CHECK_FALSE(log.holds("module options for 'test.mod.dup'"));
}

TEST_CASE("a module that provides an undeclared capability provides nothing") {
    const auto registry = dvv::ComponentRegistry::load();
    const dvv::ModuleStatus* undeclared = registry->module("test.mod.undeclared");
    REQUIRE(undeclared != nullptr);
    CHECK(undeclared->reason ==
          "Internal: load() provided undeclared capability 'test.undeclared'");
    CHECK(undeclared->provides.empty());
    CHECK(undeclared->withheld == std::vector<std::string>{"test.declared"});
    CHECK_FALSE(registry->capabilities().has("test.declared"));
    CHECK_FALSE(registry->capabilities().has("test.undeclared"));
    CHECK(undeclared->properties ==
          std::vector<dvv::ModuleProperty>{{"directory", "/fake/modules/undeclared"}});
}

TEST_CASE("load logs one line per module, with no reason and no property") {
    const CapturedLog log;
    const auto registry = dvv::ComponentRegistry::load();
    CHECK(log.holds("module 'test.mod.ready': provides test.ready; withholds test.extra"));
    CHECK(log.holds("module 'test.mod.complete': provides test.c.one, test.c.two"));
    CHECK_FALSE(log.holds("module 'test.mod.complete': provides test.c.one, test.c.two;"));
    CHECK(log.holds("module 'test.mod.options': provides nothing"));
    CHECK_FALSE(log.holds("module 'test.mod.options': provides nothing;"));
    CHECK(log.holds("module 'test.mod.throws' threw from load()"));
    CHECK(log.holds("module 'test.mod.throws': provides nothing; withholds test.thrown"));
    CHECK(log.holds("module 'test.mod.throws_other' threw from load()"));
    CHECK(log.count("module id 'test.mod.dup' is registered more than once") == 1);
    CHECK(log.count("module 'test.mod.dup': provides nothing; withholds test.dup") == 2);
    CHECK(log.holds(
        "module 'test.mod.undeclared' provided 'test.undeclared', which its info() does not list"));
    CHECK(log.holds("module 'test.mod.undeclared': provides nothing; withholds test.declared"));
    // No reason, no property and no exception text.
    CHECK_FALSE(log.holds("test.extra is switched off"));
    CHECK_FALSE(log.holds("stray text"));
    CHECK_FALSE(log.holds("unknown exception"));
    CHECK_FALSE(log.holds("/fake/modules"));  // the directory properties and the exception text
}

TEST_CASE("module states are released in reverse load order when the registry ends") {
    withModuleStates([](ModuleStates& states) { states.released.clear(); });
    auto registry = dvv::ComponentRegistry::load();
    CHECK(withModuleStates([](const ModuleStates& states) { return states.released.empty(); }));
    registry.reset();
    CHECK(withModuleStates([](const ModuleStates& states) { return states.released; }) ==
          std::vector<std::string>{"b", "a"});
    CHECK_FALSE(release_unrecorded);
}

TEST_CASE("the registry drops its own reference to module state") {
    auto registry = dvv::ComponentRegistry::load();
    CHECK_FALSE(expired(&ModuleStates::weak));
    registry.reset();
    CHECK(expired(&ModuleStates::weak));
}

TEST_CASE("state a module shares outlives the registry") {
    auto registry = dvv::ComponentRegistry::load();
    registry.reset();
    CHECK_FALSE(expired(&ModuleStates::shared));
    std::shared_ptr<void> copy;
    withModuleStates([&copy](ModuleStates& states) { copy = std::move(states.sharedCopy); });
    copy.reset();
    CHECK(expired(&ModuleStates::shared));
}

TEST_CASE("a module that provides nothing keeps its handle until the registry ends") {
    auto registry = dvv::ComponentRegistry::load();
    CHECK_FALSE(expired(&ModuleStates::idle));
    registry.reset();
    CHECK(expired(&ModuleStates::idle));
}

TEST_CASE(
    "a module that provides an undeclared capability keeps its handle until the registry "
    "ends") {
    auto registry = dvv::ComponentRegistry::load();
    CHECK_FALSE(expired(&ModuleStates::undeclared));
    registry.reset();
    CHECK(expired(&ModuleStates::undeclared));
}

TEST_CASE("two registries load their modules independently") {
    const int loads = counting_loads.load();
    const int releases = counting_releases.load();
    auto first = dvv::ComponentRegistry::load();
    auto second = dvv::ComponentRegistry::load();
    CHECK(counting_loads.load() == loads + 2);
    CHECK(counting_releases.load() == releases);
    first.reset();
    CHECK(counting_releases.load() == releases + 1);
    second.reset();
    CHECK(counting_releases.load() == releases + 2);
}

TEST_CASE("two threads load at once") {
    const int loads = counting_loads.load();
    const int releases = counting_releases.load();
    std::unique_ptr<dvv::ComponentRegistry> first;
    std::unique_ptr<dvv::ComponentRegistry> second;
    std::thread first_thread([&first] { first = dvv::ComponentRegistry::load(); });
    std::thread second_thread([&second] { second = dvv::ComponentRegistry::load(); });
    first_thread.join();
    second_thread.join();
    CHECK(counting_loads.load() == loads + 2);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->module("test.mod.counting") != nullptr);
    CHECK(second->module("test.mod.counting") != nullptr);
    CHECK(counting_releases.load() == releases);
    first.reset();
    second.reset();
    CHECK(counting_releases.load() == releases + 2);
}

TEST_CASE("modules, capabilities and createComponent from several threads at once") {
    constexpr int THREADS = 8;
    constexpr int ROUNDS = 200;
    const auto registry = dvv::ComponentRegistry::load();
    const std::size_t module_count = registry->modules().size();
    const dvv::ModuleStatus* ready = registry->module("test.mod.ready");
    REQUIRE(ready != nullptr);

    std::atomic<int> unexpected{0};
    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int thread = 0; thread < THREADS; ++thread) {
        threads.emplace_back([&] {
            for (int round = 0; round < ROUNDS; ++round) {
                if (registry->modules().size() != module_count ||
                    registry->module("test.mod.ready") != ready ||
                    !registry->capabilities().has("test.ready") ||
                    !registry->createComponent<IModuleProbe>("test.needs_ready").has_value()) {
                    unexpected.fetch_add(1);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(unexpected.load() == 0);
}
