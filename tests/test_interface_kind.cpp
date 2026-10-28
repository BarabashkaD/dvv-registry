#include <doctest/doctest.h>

#include <atomic>
#include <expected>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/registry/component_info.h"
#include "dvv/registry/component_registry.h"

namespace {

// An interface whose components must report Kind::Analyser.
class IKindedWidget {
public:
    IKindedWidget() = default;
    IKindedWidget(const IKindedWidget&) = delete;
    IKindedWidget& operator=(const IKindedWidget&) = delete;
    IKindedWidget(IKindedWidget&&) = delete;
    IKindedWidget& operator=(IKindedWidget&&) = delete;
    virtual ~IKindedWidget() = default;

    [[nodiscard]] virtual int value() const = 0;
};

// An interface without InterfaceKind.
class IPlainWidget {
public:
    IPlainWidget() = default;
    IPlainWidget(const IPlainWidget&) = delete;
    IPlainWidget& operator=(const IPlainWidget&) = delete;
    IPlainWidget(IPlainWidget&&) = delete;
    IPlainWidget& operator=(IPlainWidget&&) = delete;
    virtual ~IPlainWidget() = default;
};

}  // namespace

template <>
struct dvv::InterfaceKind<IKindedWidget> : std::integral_constant<dvv::Kind, dvv::Kind::Analyser> {
};

namespace {

using Key = dvv::CreationKey<dvv::ComponentRegistry>;
using Probed = std::expected<void, dvv::Unavailable>;

class KindedGood final : public IKindedWidget, public dvv::Managed<KindedGood> {
public:
    KindedGood(Key key, const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.kinded_good", .name = "Kinded good", .kind = dvv::Kind::Analyser};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) { return {}; }

    [[nodiscard]] int value() const override { return 7; }
};
DVV_REGISTER(IKindedWidget, KindedGood);

// Reports a kind that differs from its interface's; counts its probe calls.
class KindedBad final : public IKindedWidget, public dvv::Managed<KindedBad> {
public:
    KindedBad(Key key, const dvv::ParameterValues& /*values*/) noexcept : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "test.kinded_bad", .name = "Kinded bad", .kind = dvv::Kind::Exporter};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static Probed probe(const dvv::Capabilities& /*caps*/) {
        probe_calls.fetch_add(1);
        return {};
    }

    [[nodiscard]] int value() const override { return 0; }

    static inline std::atomic<int> probe_calls{0};
};
DVV_REGISTER(IKindedWidget, KindedBad);

const dvv::ComponentStatus* findStatus(const std::vector<dvv::ComponentStatus>& statuses,
                                       std::string_view id) {
    for (const auto& status : statuses) {
        if (status.info.id == id) {
            return &status;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("kind check: a matching kind is available") {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    const auto statuses = registry->list();
    const dvv::ComponentStatus* good = findStatus(statuses, "test.kinded_good");
    REQUIRE(good != nullptr);
    CHECK(good->available());

    auto widget = registry->createComponent<IKindedWidget>("test.kinded_good");
    REQUIRE(widget.has_value());
    CHECK((*widget)->value() == 7);
}

TEST_CASE("kind check: a different kind is unavailable with the reason") {
    const int probes_before = KindedBad::probe_calls.load();
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
    CHECK(KindedBad::probe_calls.load() == probes_before);

    const auto statuses = registry->list();
    const dvv::ComponentStatus* bad = findStatus(statuses, "test.kinded_bad");
    REQUIRE(bad != nullptr);
    REQUIRE(bad->unavailable.has_value());
    CHECK(bad->unavailable.value_or(dvv::Unavailable{}).reason ==
          "kind Exporter does not match interface kind Analyser");
    CHECK(bad->schema != nullptr);

    const auto created = registry->createComponent<IKindedWidget>("test.kinded_bad");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == dvv::Errc::Unsupported);
    CHECK(created.error().message.find("is unavailable") != std::string::npos);
}

TEST_CASE("kind check: interfaces without InterfaceKind are unchanged") {
    static_assert(dvv::HasInterfaceKind<IKindedWidget>);
    static_assert(!dvv::HasInterfaceKind<IPlainWidget>);
    static_assert(dvv::InterfaceKind<IKindedWidget>::value == dvv::Kind::Analyser);
    CHECK(true);
}
