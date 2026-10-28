#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/registry/parameters.h"

namespace {

dvv::ParameterSchema sampleSchema() {
    return dvv::ParameterSchema::Builder()
        .addInt("radius", "Radius", {.min = -5, .max = 10, .defaultValue = 3})
        .addFloat("gain", "Gain", {.min = 0.0, .max = 4.0, .defaultValue = 1.0})
        .addBool("invert", "Invert", {.defaultValue = false})
        .addEnum("mode", "Mode", {.options = {"linear", "log"}, .defaultValue = "linear"})
        .addRange("levels", "Levels",
                  {.min = 0.0, .max = 1.0, .defaultValue = {.low = 0.1, .high = 0.9}})
        .addColour("tint", "Tint", {.defaultValue = {.r = 1.0F, .g = 0.5F, .b = 0.0F, .a = 1.0F}})
        .build()
        .value();
}

std::string parseError(std::string_view name, std::string_view text) {
    const auto value = sampleSchema().parse(name, text);
    if (value.has_value()) {
        return "";
    }
    CHECK(value.error().code == dvv::Errc::Format);
    return value.error().message;
}

std::string fromJsonError(std::string_view text) {
    const auto values = sampleSchema().fromJson(dvv::json::parse(text).value());
    if (values.has_value()) {
        return "";
    }
    CHECK(values.error().code == dvv::Errc::Format);
    return values.error().message;
}

}  // namespace

TEST_CASE("parse reads every parameter type") {
    const auto schema = sampleSchema();
    CHECK(schema.parse("radius", "7").value() == dvv::ParameterValue{std::int64_t{7}});
    CHECK(schema.parse("radius", "-5").value() == dvv::ParameterValue{std::int64_t{-5}});
    CHECK(schema.parse("gain", "2.5").value() == dvv::ParameterValue{2.5});
    CHECK(schema.parse("gain", "1e-1").value() == dvv::ParameterValue{0.1});
    CHECK(schema.parse("gain", "3").value() == dvv::ParameterValue{3.0});
    CHECK(schema.parse("invert", "true").value() == dvv::ParameterValue{true});
    CHECK(schema.parse("invert", "0").value() == dvv::ParameterValue{false});
    CHECK(schema.parse("mode", "log").value() == dvv::ParameterValue{std::string("log")});
    CHECK(schema.parse("levels", "0.2,0.8").value() ==
          dvv::ParameterValue{dvv::Range{.low = 0.2, .high = 0.8}});
    CHECK(schema.parse("levels", " 0.2 , 0.8 ").value() ==
          dvv::ParameterValue{dvv::Range{.low = 0.2, .high = 0.8}});
    CHECK(schema.parse("tint", "1,0,0").value() ==
          dvv::ParameterValue{dvv::Colour{.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}});
    CHECK(schema.parse("tint", "0.1, 0.2, 0.3, 0.4").value() ==
          dvv::ParameterValue{dvv::Colour{.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 0.4F}});
}

TEST_CASE("parse names the problem") {
    CHECK(parseError("size", "1") == "unknown parameter 'size'");
    CHECK(parseError("radius", "7.5") == "parameter 'radius': '7.5' is not an integer");
    CHECK(parseError("radius", "") == "parameter 'radius': '' is not an integer");
    CHECK(parseError("radius", "99999999999999999999") ==
          "parameter 'radius': '99999999999999999999' is not an integer");
    CHECK(parseError("radius", "11") == "parameter 'radius': 11 is outside [-5, 10]");
    CHECK(parseError("gain", "abc") == "parameter 'gain': 'abc' is not a number");
    CHECK(parseError("gain", "2x") == "parameter 'gain': '2x' is not a number");
    CHECK(parseError("gain", "nan") == "parameter 'gain': not a finite number");
    CHECK(parseError("invert", "maybe") ==
          "parameter 'invert': 'maybe' is not a boolean (true, false, 1 or 0)");
    CHECK(parseError("mode", "cubic") == "parameter 'mode': 'cubic' is not one of: linear, log");
    CHECK(parseError("levels", "0.5") == "parameter 'levels': '0.5' is not a range (low,high)");
    CHECK(parseError("levels", "0.9,0.1") == "parameter 'levels': low 0.9 is above high 0.1");
    CHECK(parseError("tint", "1,1") ==
          "parameter 'tint': '1,1' is not a colour (r,g,b or r,g,b,a)");
    CHECK(parseError("tint", "2,0,0") == "parameter 'tint': colour components must be in [0, 1]");
}

TEST_CASE("format writes the text parse reads") {
    using dvv::ParameterSchema;
    CHECK(ParameterSchema::format(std::int64_t{-3}) == "-3");
    CHECK(ParameterSchema::format(0.1) == "0.1");
    CHECK(ParameterSchema::format(1.0) == "1");
    CHECK(ParameterSchema::format(true) == "true");
    CHECK(ParameterSchema::format(false) == "false");
    CHECK(ParameterSchema::format(std::string("log")) == "log");
    CHECK(ParameterSchema::format(dvv::Range{.low = 0.25, .high = 0.5}) == "0.25,0.5");
    CHECK(ParameterSchema::format(dvv::Colour{.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 1.0F}) ==
          "0.1,0.2,0.3,1");

    const auto schema = sampleSchema();
    const dvv::ParameterValues samples{
        {"radius", std::int64_t{-5}},
        {"gain", 1.0 / 3.0},
        {"invert", true},
        {"mode", std::string("log")},
        {"levels", dvv::Range{.low = 0.1, .high = 0.7}},
        {"tint", dvv::Colour{.r = 0.1F, .g = 0.7F, .b = 1.0F / 3.0F, .a = 0.0F}},
    };
    for (const auto& [name, value] : samples) {
        CAPTURE(name);
        CHECK(schema.parse(name, ParameterSchema::format(value)).value() == value);
    }
}

TEST_CASE("values round-trip through JSON") {
    const auto schema = sampleSchema();
    const dvv::ParameterValues values{
        {"radius", std::int64_t{7}},
        {"gain", 2.5},
        {"invert", true},
        {"mode", std::string("log")},
        {"levels", dvv::Range{.low = 0.1, .high = 0.9}},
        {"tint", dvv::Colour{.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 1.0F}},
    };
    const dvv::json::Value object = dvv::ParameterSchema::toJson(values);
    CHECK(dvv::json::dump(object) ==
          R"({"gain":2.5,"invert":true,"levels":[0.1,0.9],"mode":"log","radius":7,)"
          R"("tint":[0.1,0.2,0.3,1.0]})");
    CHECK(schema.fromJson(object).value() == values);
    CHECK(schema.fromJson(dvv::json::Value::object()).value().empty());
}

TEST_CASE("fromJson accepts an integer for a float and an RGB colour") {
    const auto values =
        sampleSchema().fromJson(dvv::json::parse(R"({"gain": 2, "tint": [0, 1, 0]})").value());
    REQUIRE(values.has_value());
    CHECK(values->at("gain") == dvv::ParameterValue{2.0});
    CHECK(values->at("tint") ==
          dvv::ParameterValue{dvv::Colour{.r = 0.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F}});
}

TEST_CASE("fromJson names the problem") {
    CHECK(fromJsonError("[]") == "parameters: expected a JSON object, got []");
    CHECK(fromJsonError(R"({"size": 1})") == "unknown parameter 'size'");
    CHECK(fromJsonError(R"({"radius": 1.5})") ==
          "parameter 'radius': expected an integer, got 1.5");
    CHECK(fromJsonError(R"({"radius": 18446744073709551615})") ==
          "parameter 'radius': expected an integer, got 18446744073709551615");
    CHECK(fromJsonError(R"({"radius": 11})") == "parameter 'radius': 11 is outside [-5, 10]");
    CHECK(fromJsonError(R"({"gain": true})") == "parameter 'gain': expected a number, got true");
    CHECK(fromJsonError(R"({"invert": 1})") == "parameter 'invert': expected a boolean, got 1");
    CHECK(fromJsonError(R"({"mode": 2})") == "parameter 'mode': expected a string, got 2");
    CHECK(fromJsonError(R"({"mode": "cubic"})") ==
          "parameter 'mode': 'cubic' is not one of: linear, log");
    CHECK(fromJsonError(R"({"levels": [0.1]})") ==
          "parameter 'levels': expected [low, high], got [0.1]");
    CHECK(fromJsonError(R"({"levels": [0.1, "x"]})") ==
          R"(parameter 'levels': expected [low, high], got [0.1,"x"])");
    CHECK(fromJsonError(R"({"tint": {"r": 1}})") ==
          R"(parameter 'tint': expected [r, g, b] or [r, g, b, a], got {"r":1})");
    CHECK(fromJsonError(R"({"tint": [1e300, 0, 0]})") ==
          "parameter 'tint': colour components must be in [0, 1]");
}
