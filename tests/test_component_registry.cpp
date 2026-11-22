#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
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

// An interface without any component.
class IUnregistered {
public:
    IUnregistered() = default;
    IUnregistered(const IUnregistered&) = delete;
    IUnregistered& operator=(const IUnregistered&) = delete;
    IUnregistered(IUnregistered&&) = delete;
    IUnregistered& operator=(IUnregistered&&) = delete;
    virtual ~IUnregistered() = default;
};

template <class T>
concept HasSchemaField = requires(T status) { status.schema; };

using Key = dvv::CreationKey<dvv::ComponentRegistry>;
using Probed = std::expected<void, dvv::Unavailable>;

// A greeter; always available.
class Greeter final : public IGreeter, public dvv::Managed<Greeter> {
public:
    explicit Greeter(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.greeter", .name = "Greeter", .version = 2};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return "hello"; }
};
DVV_REGISTER(IGreeter, Greeter);

// Needs a capability; counts its probe calls.
class GpuGreeter final : public IGreeter, public dvv::Managed<GpuGreeter> {
public:
    explicit GpuGreeter(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.gpu_greeter", .name = "GPU greeter"};
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
    explicit ThrowingProbe(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.throwing_probe", .name = "Throwing probe"};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) {
        throw std::runtime_error("probe boom");
    }

    [[nodiscard]] std::string greet() const override { return ""; }
};
DVV_REGISTER(IGreeter, ThrowingProbe);

class ThrowingConstructor final : public IGreeter, public dvv::Managed<ThrowingConstructor> {
public:
    explicit ThrowingConstructor(Key key) : Managed(std::move(key)) {
        throw std::runtime_error("constructor boom");
    }

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.throwing_constructor", .name = "Throwing constructor"};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return ""; }
};
DVV_REGISTER(IGreeter, ThrowingConstructor);

// Its constructor runs out of memory; counts the constructor calls.
class BadAlloc final : public IGreeter, public dvv::Managed<BadAlloc> {
public:
    explicit BadAlloc(Key key) : Managed(std::move(key)) {
        constructor_calls.fetch_add(1);
        throw std::bad_alloc();
    }

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.bad_alloc", .name = "Bad alloc"};
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] std::string greet() const override { return ""; }

    static inline std::atomic<int> constructor_calls{0};
};
DVV_REGISTER(IGreeter, BadAlloc);

class Counter final : public ICounter, public dvv::Managed<Counter> {
public:
    explicit Counter(Key key) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept { return {.id = "test.counter", .name = "Counter"}; }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int count() const override { return 42; }
};
DVV_REGISTER(ICounter, Counter);

// Two classes claiming the same id.
template <int N>
class Duplicate final : public ICounter, public dvv::Managed<Duplicate<N>> {
public:
    explicit Duplicate(Key key) noexcept : dvv::Managed<Duplicate<N>>(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.duplicate", .name = "Duplicate"};
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

TEST_CASE("list() returns every registered component, sorted by id") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto all = registry->list();
    CHECK(std::ranges::is_sorted(
        all, {}, [](const dvv::ComponentStatus& status) { return status.info.id; }));

    // The binary also links test_modules.cpp's components, so there is no exact total.
    const auto occurrences = [](const std::vector<dvv::ComponentStatus>& statuses,
                                std::string_view id) {
        return std::ranges::count_if(
            statuses, [id](const dvv::ComponentStatus& status) { return status.info.id == id; });
    };
    for (const auto& by_interface : {registry->list<IGreeter>(), registry->list<ICounter>()}) {
        for (const auto& status : by_interface) {
            CHECK(occurrences(all, status.info.id) == occurrences(by_interface, status.info.id));
        }
    }

    const dvv::ComponentStatus* greeter = findStatus(all, "test.greeter");
    REQUIRE(greeter != nullptr);
    CHECK(greeter->info == Greeter::info());
    CHECK(greeter->available());
}

TEST_CASE("list<I>() returns the components registered for I, sorted by id, available or not") {
    static_assert(!HasSchemaField<dvv::ComponentStatus>);
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    std::vector<std::string_view> ids;
    for (const auto& status : registry->list<IGreeter>()) {
        ids.push_back(status.info.id);
    }
    CHECK(ids == std::vector<std::string_view>{"test.bad_alloc", "test.gpu_greeter", "test.greeter",
                                               "test.throwing_constructor", "test.throwing_probe"});
    CHECK(registry->list<ICounter>().size() == 3);  // no greeter among them
    CHECK(registry->list<IUnregistered>().empty());
}

TEST_CASE("probes decide availability and run once per registry") {
    const int before = GpuGreeter::probe_calls.load();
    const auto without = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(GpuGreeter::probe_calls.load() == before + 1);
    CHECK(reasonOf(*without, "test.gpu_greeter") == "missing capability 'gpu.compute'");
    const auto without_statuses = without->list();
    const dvv::ComponentStatus* without_greeter = findStatus(without_statuses, "test.gpu_greeter");
    REQUIRE(without_greeter != nullptr);
    CHECK(without_greeter->unavailable.value_or(dvv::Unavailable{}).capability == "gpu.compute");

    const auto with = dvv::ComponentRegistry::create(dvv::Capabilities{"gpu.compute"});
    CHECK(GpuGreeter::probe_calls.load() == before + 2);
    CHECK(reasonOf(*with, "test.gpu_greeter").empty());
    auto greeter = with->createComponent<IGreeter>("test.gpu_greeter");
    REQUIRE(greeter.has_value());
    CHECK((*greeter)->greet() == "hello from the GPU");
    static_cast<void>(with->list());
    CHECK(GpuGreeter::probe_calls.load() == before + 2);
}

TEST_CASE("a throwing probe makes a component unavailable") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(reasonOf(*registry, "test.throwing_probe") == "Internal: probe() threw: probe boom");

    const auto created = registry->createComponent<IGreeter>("test.throwing_probe");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == dvv::Errc::Unsupported);
    CHECK(created.error().message ==
          "component 'test.throwing_probe' is unavailable: Internal: probe() threw: probe boom");
}

TEST_CASE("a duplicated id disables every component that claims it") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto duplicates =
        std::ranges::count_if(registry->list<ICounter>(), [](const dvv::ComponentStatus& status) {
            return status.info.id == "test.duplicate" && status.unavailable &&
                   status.unavailable->reason == "Internal: duplicate component id: test.duplicate";
        });
    CHECK(duplicates == 2);
    const auto created = registry->createComponent<ICounter>("test.duplicate");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == dvv::Errc::Unsupported);
}

TEST_CASE("createComponent builds a component the registry owns") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const std::size_t live_before = lifetimes.liveCount();

    auto greeter = registry->createComponent<IGreeter>("test.greeter");
    REQUIRE(greeter.has_value());
    CHECK((*greeter)->greet() == "hello");

    auto counter = registry->createComponent<ICounter>("test.counter");
    REQUIRE(counter.has_value());
    CHECK((*counter)->count() == 42);
    CHECK(lifetimes.liveCount() == live_before + 2);

    // The registry is the recorded owner of what it creates.
    const auto* managed = dynamic_cast<const dvv::ManagedObject*>(greeter->get());
    REQUIRE(managed != nullptr);
    CHECK(managed->ownerId() == registry->objectId());

    greeter->reset();
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

TEST_CASE("create records its caller as the creation site") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const bool previous = lifetimes.detailed();
    lifetimes.setDetailed(true);
    const auto line = std::source_location::current().line() + 1;
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto objects = lifetimes.liveObjects();
    lifetimes.setDetailed(previous);

    const auto found = std::ranges::find(objects, registry->objectId(), &dvv::LiveObjectInfo::id);
    REQUIRE(found != objects.end());
    CHECK(found->createdAt.has_value());
    const std::source_location where = found->createdAt.value_or(std::source_location());
    CHECK(std::string_view(where.file_name()).ends_with("test_component_registry.cpp"));
    CHECK(where.line() == line);
}

TEST_CASE("createComponent(id, where) records where") {
    auto& lifetimes = dvv::LifetimeRegistry::instance();
    const bool previous = lifetimes.detailed();
    lifetimes.setDetailed(true);
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const std::source_location where = std::source_location::current();
    auto greeter = registry->createComponent<IGreeter>("test.greeter", where);
    lifetimes.setDetailed(previous);
    REQUIRE(greeter.has_value());

    const auto* managed = dynamic_cast<const dvv::ManagedObject*>(greeter->get());
    REQUIRE(managed != nullptr);
    const auto objects = lifetimes.liveObjects();
    const auto found = std::ranges::find(objects, managed->objectId(), &dvv::LiveObjectInfo::id);
    REQUIRE(found != objects.end());
    CHECK(found->createdAt.has_value());
    const std::source_location created_at = found->createdAt.value_or(std::source_location());
    CHECK(std::string_view(created_at.file_name()).ends_with("test_component_registry.cpp"));
    CHECK(created_at.line() == where.line());
}

TEST_CASE("createComponent reports unknown ids and other interfaces") {
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
                const auto greeter = registry->createComponent<IGreeter>("test.greeter");
                if (greeter.has_value() && (*greeter)->greet() == "hello") {
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
