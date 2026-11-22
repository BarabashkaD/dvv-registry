#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_info.h"

namespace {

template <class T>
concept HasKindField = requires(T info) { info.kind; };

}  // namespace

TEST_CASE("capabilities hold names") {
    dvv::Capabilities caps;
    CHECK_FALSE(caps.has("gpu.compute"));
    caps.add("gpu.compute");
    caps.add("gpu.compute");
    caps.add("gpu.float16");
    CHECK(caps.has("gpu.compute"));
    CHECK(caps.has("gpu.float16"));
    CHECK_FALSE(caps.has("gpu"));
    CHECK(caps.names() == std::vector<std::string>{"gpu.compute", "gpu.float16"});
}

TEST_CASE("capabilities from a list") {
    const dvv::Capabilities caps{"ffmpeg", "gpu.compute"};
    CHECK(caps.has("ffmpeg"));
    CHECK(caps.has("gpu.compute"));
    CHECK(caps.names().size() == 2);
}

TEST_CASE("require names the first missing capability") {
    const dvv::Capabilities caps{"gpu.compute"};
    CHECK(dvv::require(caps, {}).has_value());
    CHECK(dvv::require(caps, {"gpu.compute"}).has_value());

    const auto missing = dvv::require(caps, {"gpu.compute", "gpu.float16", "ffmpeg"});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().reason == "missing capability 'gpu.float16'");
}

TEST_CASE("component info is a comparable value") {
    constexpr dvv::ComponentInfo PSNR{.id = "psnr", .name = "PSNR", .version = 1};
    static_assert(!HasKindField<dvv::ComponentInfo>);
    static_assert(PSNR.id == "psnr");
    dvv::ComponentInfo copy = PSNR;
    CHECK(copy == PSNR);
    copy.version = 2;
    CHECK(copy != PSNR);
}

TEST_CASE("require names the missing capability") {
    const dvv::Capabilities caps{"a"};
    const auto result = dvv::require(caps, {"a", "b"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().capability == "b");
    CHECK(result.error().reason == "missing capability 'b'");
}

TEST_CASE("an Unavailable from a reason alone has no capability") {
    const dvv::Unavailable unavailable{"text"};
    CHECK(unavailable.capability.empty());
}
