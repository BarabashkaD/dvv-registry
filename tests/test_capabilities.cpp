#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_info.h"

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

TEST_CASE("component kinds have names") {
    CHECK(dvv::kindName(dvv::Kind::Source) == "Source");
    CHECK(dvv::kindName(dvv::Kind::FormatDetector) == "FormatDetector");
    CHECK(dvv::kindName(dvv::Kind::Exporter) == "Exporter");
    CHECK(dvv::kindName(dvv::Kind::Tool) == "Tool");
    CHECK(dvv::kindName(dvv::Kind::Metric) == "Metric");
    CHECK(dvv::kindName(dvv::Kind::Analyser) == "Analyser");
    CHECK(dvv::kindName(dvv::Kind::Processor) == "Processor");
}

TEST_CASE("component info is a comparable value") {
    constexpr dvv::ComponentInfo PSNR{
        .id = "psnr", .name = "PSNR", .kind = dvv::Kind::Metric, .version = 1};
    static_assert(PSNR.id == "psnr");
    dvv::ComponentInfo copy = PSNR;
    CHECK(copy == PSNR);
    copy.version = 2;
    CHECK(copy != PSNR);
}
