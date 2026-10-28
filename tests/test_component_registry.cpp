#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/registry/component_registry.h"

namespace {

class IGreeter {
public:
    IGreeter() = default;
    IGreeter(const IGreeter&) = delete;
    IGreeter& operator=(const IGreeter&) = delete;
    IGreeter(IGreeter&&) = delete;
    IGreeter& operator=(IGreeter&&) = delete;
    virtual ~IGreeter() = default;

    [[nodiscard]] virtual std::string greet() const = 0;
};

class ICounter {
public:
    ICounter() = default;
    ICounter(const ICounter&) = delete;
    ICounter& operator=(const ICounter&) = delete;
    ICounter(ICounter&&) = delete;
    ICounter& operator=(ICounter&&) = delete;
    virtual ~ICounter() = default;

    [[nodiscard]] virtual int count() const = 0;
};

using Key = dvv::CreationKey<dvv::ComponentRegistry>;
using Probed = std::expected<void, dvv::Unavailable>;

// A greeter with parameters; always available.
class Greeter final : public IGreeter, public dvv::Managed<Greeter> {
public:
    Greeter(Key key, const dvv::ParameterValues& values)
        : Managed(std::move(key)),
          greeting_(std::get<std::string>(values.at("greeting"))),
          repeat_(std::get<std::int64_t>(values.at("repeat"))) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.greeter", .name = "Greeter", .kind = dvv::Kind::Tool, .version = 2};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA =
            dvv::ParameterSchema::Builder()
                .addEnum("greeting", "Greeting",
                         {.options = {"hello", "hi"}, .defaultValue = "hello"})
                .addInt("repeat", "Repeat", {.min = 1, .max = 3, .defaultValue = 1})
                .build()
                .value();
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override {
        std::string text;
        for (std::int64_t i = 0; i < repeat_; ++i) {
            text += text.empty() ? greeting_ : " " + greeting_;
        }
        return text;
    }

private:
    std::string greeting_;
    std::int64_t repeat_;
};
DVV_REGISTER(IGreeter, Greeter);

// Needs a capability; counts its probe calls.
class GpuGreeter final : public IGreeter, public dvv::Managed<GpuGreeter> {
public:
    GpuGreeter(Key key, const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.gpu_greeter", .name = "GPU greeter", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& caps) {
        probe_calls.fetch_add(1);
        return dvv::require(caps, {"gpu.compute"});
    }

    [[nodiscard]] std::string greet() const override { return "hello from the GPU"; }

    static inline std::atomic<int> probe_calls{0};
};
DVV_REGISTER(IGreeter, GpuGreeter);

class ThrowingProbe final : public IGreeter, public dvv::Managed<ThrowingProbe> {
public:
    ThrowingProbe(Key key, const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.throwing_probe", .name = "Throwing probe", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) {
        throw std::runtime_error("probe boom");
    }

    [[nodiscard]] std::string greet() const override { return ""; }
};
DVV_REGISTER(IGreeter, ThrowingProbe);

// Its schema has a duplicate parameter, so schema() throws from build().value().
class BrokenSchema final : public IGreeter, public dvv::Managed<BrokenSchema> {
public:
    BrokenSchema(Key key, const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.broken_schema", .name = "Broken schema", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA = dvv::ParameterSchema::Builder()
                                                       .addBool("flag", "Flag", {})
                                                       .addBool("flag", "Flag again", {})
                                                       .build()
                                                       .value();
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return ""; }
};
DVV_REGISTER(IGreeter, BrokenSchema);

class ThrowingConstructor final : public IGreeter, public dvv::Managed<ThrowingConstructor> {
public:
    ThrowingConstructor(Key key, const dvv::ParameterValues& /*values*/) : Managed(std::move(key)) {
        throw std::runtime_error("constructor boom");
    }

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.throwing_constructor",
                .name = "Throwing constructor",
                .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return ""; }
};
DVV_REGISTER(IGreeter, ThrowingConstructor);

// Its constructor runs out of memory; counts the constructor calls.
class BadAlloc final : public IGreeter, public dvv::Managed<BadAlloc> {
public:
    BadAlloc(Key key, const dvv::ParameterValues& /*values*/) : Managed(std::move(key)) {
        constructor_calls.fetch_add(1);
        throw std::bad_alloc();
    }

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.bad_alloc", .name = "Bad alloc", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return ""; }

    static inline std::atomic<int> constructor_calls{0};
};
DVV_REGISTER(IGreeter, BadAlloc);

class Counter final : public ICounter, public dvv::Managed<Counter> {
public:
    Counter(Key key, const dvv::ParameterValues& /*values*/) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.counter", .name = "Counter", .kind = dvv::Kind::Metric};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int count() const override { return 42; }
};
DVV_REGISTER(ICounter, Counter);

// Two classes claiming the same id.
template <int N>
class Duplicate final : public ICounter, public dvv::Managed<Duplicate<N>> {
public:
    Duplicate(Key key, const dvv::ParameterValues& /*values*/) noexcept
        : dvv::Managed<Duplicate<N>>(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.duplicate", .name = "Duplicate", .kind = dvv::Kind::Metric};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int count() const override { return N; }
};
using FirstDuplicate = Duplicate<1>;
using SecondDuplicate = Duplicate<2>;
DVV_REGISTER(ICounter, FirstDuplicate);
DVV_REGISTER(ICounter, SecondDuplicate);

const dvv::ComponentStatus* findStatus(const std::vector<dvv::ComponentStatus>& statuses,
                                       std::string_view id) {
    const auto found = std::ranges::find(
        statuses, id, [](const dvv::ComponentStatus& status) { return status.info.id; });
    return found != statuses.end() ? &*found : nullptr;
}

std::string reasonOf(const dvv::ComponentRegistry& registry, std::string_view id) {
    const auto statuses = registry.list();
    const dvv::ComponentStatus* status = findStatus(statuses, id);
    REQUIRE(status != nullptr);
    return status->unavailable ? status->unavailable->reason : "";
}

}  // namespace

TEST_CASE("the registry lists registered components by kind, sorted by id") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});

    const auto tools = registry->list(dvv::Kind::Tool);
    std::vector<std::string_view> ids;
    for (const auto& status : tools) {
        CHECK(status.info.kind == dvv::Kind::Tool);
        ids.push_back(status.info.id);
    }
    CHECK(ids == std::vector<std::string_view>{"test.bad_alloc", "test.broken_schema",
                                               "test.gpu_greeter", "test.greeter",
                                               "test.throwing_constructor", "test.throwing_probe"});

    const dvv::ComponentStatus* greeter = findStatus(tools, "test.greeter");
    REQUIRE(greeter != nullptr);
    CHECK(greeter->info == Greeter::info());
    CHECK(greeter->available());
    CHECK(greeter->schema == &Greeter::schema());

    const auto metrics = registry->list(dvv::Kind::Metric);
    CHECK(metrics.size() == 3);  // test.counter and the two test.duplicate
    CHECK(findStatus(metrics, "test.counter") != nullptr);
    CHECK(registry->list(dvv::Kind::Source).empty());
    // + 2: test.kinded_good (Analyser) and test.kinded_bad (Exporter) of test_interface_kind.cpp.
    CHECK(registry->list().size() == tools.size() + metrics.size() + 2);
}

TEST_CASE("probes decide availability and run once per registry") {
    const int before = GpuGreeter::probe_calls.load();
    const auto without = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(GpuGreeter::probe_calls.load() == before + 1);
    CHECK(reasonOf(*without, "test.gpu_greeter") == "missing capability 'gpu.compute'");

    const auto with = dvv::ComponentRegistry::create(dvv::Capabilities{"gpu.compute"});
    CHECK(GpuGreeter::probe_calls.load() == before + 2);
    CHECK(reasonOf(*with, "test.gpu_greeter").empty());
    auto greeter = with->createComponent<IGreeter>("test.gpu_greeter");
    REQUIRE(greeter.has_value());
    CHECK((*greeter)->greet() == "hello from the GPU");
    static_cast<void>(with->list());
    CHECK(GpuGreeter::probe_calls.load() == before + 2);
}

TEST_CASE("throwing probes and schemas make a component unavailable") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(reasonOf(*registry, "test.throwing_probe") == "Internal: probe() threw: probe boom");
    // The reason carries the Error of the failed build().value(), with its source location.
    const std::string schema_reason = reasonOf(*registry, "test.broken_schema");
    const std::string_view expected =
        "Internal: schema() threw: Internal: parameter schema: duplicate parameter 'flag'";
    CHECK(schema_reason.starts_with(expected));
    const auto statuses = registry->list();
    const dvv::ComponentStatus* broken = findStatus(statuses, "test.broken_schema");
    REQUIRE(broken != nullptr);
    CHECK(broken->schema == nullptr);

    const auto created = registry->createComponent<IGreeter>("test.throwing_probe");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == dvv::Errc::Unsupported);
    CHECK(created.error().message ==
          "component 'test.throwing_probe' is unavailable: Internal: probe() threw: probe boom");
}

TEST_CASE("a duplicated id disables every component that claims it") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto metrics = registry->list(dvv::Kind::Metric);
    const auto duplicates = std::ranges::count_if(metrics, [](const dvv::ComponentStatus& status) {
        return status.info.id == "test.duplicate" && status.unavailable &&
               status.unavailable->reason == "Internal: duplicate component id: test.duplicate";
    });
    CHECK(duplicates == 2);
    const auto created = registry->createComponent<ICounter>("test.duplicate");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == dvv::Errc::Unsupported);
}

TEST_CASE("createComponent builds a component with validated and completed parameters") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const std::size_t live_before = lifetimes.liveCount();

    auto plain = registry->createComponent<IGreeter>("test.greeter");
    REQUIRE(plain.has_value());
    CHECK((*plain)->greet() == "hello");

    auto custom = registry->createComponent<IGreeter>(
        "test.greeter", {{"greeting", std::string("hi")}, {"repeat", std::int64_t{3}}});
    REQUIRE(custom.has_value());
    CHECK((*custom)->greet() == "hi hi hi");
    CHECK(lifetimes.liveCount() == live_before + 2);

    // The registry is the recorded owner of what it creates.
    const auto* managed = dynamic_cast<const dvv::ManagedObject*>(custom->get());
    REQUIRE(managed != nullptr);
    CHECK(managed->ownerId() == registry->objectId());

    auto counter = registry->createComponent<ICounter>("test.counter");
    REQUIRE(counter.has_value());
    CHECK((*counter)->count() == 42);

    plain->reset();
    custom->reset();
    counter->reset();
    CHECK(lifetimes.liveCount() == live_before);
}

TEST_CASE("createComponent records its caller as the creation site") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const bool previous = lifetimes.detailed();
    lifetimes.setDetailed(true);
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto line = std::source_location::current().line() + 1;
    auto greeter = registry->createComponent<IGreeter>("test.greeter");
    lifetimes.setDetailed(previous);
    REQUIRE(greeter.has_value());

    const auto* managed = dynamic_cast<const dvv::ManagedObject*>(greeter->get());
    REQUIRE(managed != nullptr);
    const auto objects = lifetimes.liveObjects();
    const auto found = std::ranges::find(objects, managed->objectId(), &dvv::LiveObjectInfo::id);
    REQUIRE(found != objects.end());
    CHECK(found->createdAt.has_value());
    const std::source_location where = found->createdAt.value_or(std::source_location());
    CHECK(std::string_view(where.file_name()).ends_with("test_component_registry.cpp"));
    CHECK(where.line() == line);
}

TEST_CASE("createComponent reports unknown ids, other interfaces and invalid values") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});

    const auto unknown = registry->createComponent<IGreeter>("test.missing");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == dvv::Errc::Unsupported);
    CHECK(unknown.error().message == "unknown component 'test.missing'");

    const auto wrong_interface = registry->createComponent<ICounter>("test.greeter");
    REQUIRE_FALSE(wrong_interface.has_value());
    CHECK(wrong_interface.error().code == dvv::Errc::Unsupported);
    CHECK(wrong_interface.error().message.starts_with(
        "component 'test.greeter' does not implement "));
    CHECK(wrong_interface.error().message.find("ICounter") != std::string::npos);

    const auto invalid =
        registry->createComponent<IGreeter>("test.greeter", {{"repeat", std::int64_t{9}}});
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == dvv::Errc::Format);
    CHECK(invalid.error().message ==
          "component 'test.greeter': parameter 'repeat': 9 is outside [1, 3]");
}

TEST_CASE("a throwing constructor disables the component for the registry's lifetime") {
    const std::size_t live_before = dvv::LifetimeRegistry::instance().liveCount();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(reasonOf(*registry, "test.throwing_constructor").empty());

    const auto first = registry->createComponent<IGreeter>("test.throwing_constructor");
    REQUIRE_FALSE(first.has_value());
    CHECK(first.error().code == dvv::Errc::Internal);
    CHECK(first.error().message ==
          "component 'test.throwing_constructor' failed: constructor boom");
    CHECK(reasonOf(*registry, "test.throwing_constructor") ==
          "Internal: constructor threw: constructor boom");

    const auto second = registry->createComponent<IGreeter>("test.throwing_constructor");
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == dvv::Errc::Unsupported);

    // Other registries are not affected.
    const auto fresh = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(reasonOf(*fresh, "test.throwing_constructor").empty());
    // The registries themselves are the only live objects this test added.
    CHECK(dvv::LifetimeRegistry::instance().liveCount() == live_before + 2);
}

TEST_CASE("std::bad_alloc from a constructor is out of memory and does not disable the component") {
    const std::size_t live_before = dvv::LifetimeRegistry::instance().liveCount();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const int calls_before = BadAlloc::constructor_calls.load();

    const auto first = registry->createComponent<IGreeter>("test.bad_alloc");
    REQUIRE_FALSE(first.has_value());
    CHECK(first.error().code == dvv::Errc::OutOfMemory);
    CHECK(first.error().message == "component 'test.bad_alloc': out of memory");
    CHECK(reasonOf(*registry, "test.bad_alloc").empty());
    CHECK(BadAlloc::constructor_calls.load() == calls_before + 1);

    // The component was not disabled: a second createComponent still reaches the constructor.
    const auto second = registry->createComponent<IGreeter>("test.bad_alloc");
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == dvv::Errc::OutOfMemory);
    CHECK(BadAlloc::constructor_calls.load() == calls_before + 2);
    CHECK(dvv::LifetimeRegistry::instance().liveCount() == live_before + 1);
}

// Run under the TSan preset: construction runs outside the registry's mutex, while a throwing
// constructor disables its entry under it.
TEST_CASE("the registry creates and lists components from several threads at once") {
    constexpr int THREADS = 6;
    constexpr int ROUNDS = 200;
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const std::size_t live_before = lifetimes.liveCount();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const std::size_t listed = registry->list().size();

    std::atomic<int> greeted{0};
    std::atomic<int> internal{0};     // the throwing constructor ran
    std::atomic<int> unsupported{0};  // it was already disabled
    std::atomic<int> unexpected{0};
    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int thread = 0; thread < THREADS; ++thread) {
        threads.emplace_back([&] {
            for (int round = 0; round < ROUNDS; ++round) {
                const auto greeter = registry->createComponent<IGreeter>(
                    "test.greeter", {{"repeat", std::int64_t{2}}});
                if (greeter.has_value() && (*greeter)->greet() == "hello hello") {
                    greeted.fetch_add(1);
                } else {
                    unexpected.fetch_add(1);
                }
                const auto thrown =
                    registry->createComponent<IGreeter>("test.throwing_constructor");
                if (!thrown.has_value() && thrown.error().code == dvv::Errc::Internal) {
                    internal.fetch_add(1);
                } else if (!thrown.has_value() && thrown.error().code == dvv::Errc::Unsupported) {
                    unsupported.fetch_add(1);
                } else {
                    unexpected.fetch_add(1);
                }
                if (registry->list().size() != listed) {
                    unexpected.fetch_add(1);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    CHECK(unexpected.load() == 0);
    CHECK(greeted.load() == THREADS * ROUNDS);
    CHECK(internal.load() >= 1);
    CHECK(internal.load() + unsupported.load() == THREADS * ROUNDS);
    CHECK(reasonOf(*registry, "test.throwing_constructor") ==
          "Internal: constructor threw: constructor boom");
    // Every component was destroyed again; only the registry is left.
    CHECK(lifetimes.liveCount() == live_before + 1);
}

TEST_CASE("the registry is a managed object") {
    static_assert(dvv::ManagedType<dvv::ComponentRegistry>);
    static_assert(dvv::Component<Greeter, IGreeter>);
    static_assert(!dvv::Component<Greeter, ICounter>);
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(registry->typeName().find("ComponentRegistry") != std::string_view::npos);
}
