#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/registry/parameters.h"

namespace {

dvv::ParameterSchema sampleSchema() {
    return dvv::ParameterSchema::Builder()
        .addInt("radius", "Radius", {.min = 1, .max = 10, .defaultValue = 3})
        .addFloat("gain", "Gain", {.min = 0.0, .max = 4.0, .defaultValue = 1.0})
        .addBool("invert", "Invert", {.defaultValue = false})
        .addEnum("mode", "Mode", {.options = {"linear", "log"}, .defaultValue = "linear"})
        .addRange("levels", "Levels",
                  {.min = 0.0, .max = 1.0, .defaultValue = {.low = 0.1, .high = 0.9}})
        .addColour("tint", "Tint", {.defaultValue = {.r = 1.0F, .g = 0.5F, .b = 0.0F, .a = 1.0F}})
        .build()
        .value();
}

// Builds the schema that `add` declares and returns the build error message, or "" when it builds.
template <class Add>
std::string buildError(Add add) {
    dvv::ParameterSchema::Builder builder;
    add(builder);
    const auto schema = builder.build();
    if (schema.has_value()) {
        return "";
    }
    CHECK(schema.error().code == dvv::Errc::Internal);
    return schema.error().message;
}

std::string validateError(const dvv::ParameterSchema& schema, const dvv::ParameterValues& values) {
    const auto result = schema.validate(values);
    if (result.has_value()) {
        return "";
    }
    CHECK(result.error().code == dvv::Errc::Format);
    return result.error().message;
}

}  // namespace

TEST_CASE("a schema keeps its parameters in order") {
    const auto schema = sampleSchema();
    REQUIRE(schema.parameters().size() == 6);
    CHECK(schema.parameters()[0].name == "radius");
    CHECK(schema.parameters()[0].label == "Radius");
    CHECK(schema.parameters()[0].type() == dvv::ParameterType::Int);
    CHECK(schema.parameters()[1].type() == dvv::ParameterType::Float);
    CHECK(schema.parameters()[2].type() == dvv::ParameterType::Bool);
    CHECK(schema.parameters()[3].type() == dvv::ParameterType::Enum);
    CHECK(schema.parameters()[4].type() == dvv::ParameterType::Range);
    CHECK(schema.parameters()[5].type() == dvv::ParameterType::Colour);

    const dvv::Parameter* mode = schema.find("mode");
    REQUIRE(mode != nullptr);
    const auto& spec = std::get<dvv::EnumParameter>(mode->spec);
    CHECK(spec.options == std::vector<std::string>{"linear", "log"});
    CHECK(schema.find("missing") == nullptr);
}

TEST_CASE("defaults hold every parameter") {
    const auto defaults = sampleSchema().defaults();
    CHECK(defaults.size() == 6);
    CHECK(defaults.at("radius") == dvv::ParameterValue{std::int64_t{3}});
    CHECK(defaults.at("gain") == dvv::ParameterValue{1.0});
    CHECK(defaults.at("invert") == dvv::ParameterValue{false});
    CHECK(defaults.at("mode") == dvv::ParameterValue{std::string("linear")});
    CHECK(defaults.at("levels") == dvv::ParameterValue{dvv::Range{.low = 0.1, .high = 0.9}});
    CHECK(defaults.at("tint") ==
          dvv::ParameterValue{dvv::Colour{.r = 1.0F, .g = 0.5F, .b = 0.0F, .a = 1.0F}});
    CHECK(dvv::ParameterSchema().defaults().empty());
}

TEST_CASE("parameter types have names") {
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Int) == "int");
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Float) == "float");
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Bool) == "bool");
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Enum) == "enum");
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Range) == "range");
    CHECK(dvv::parameterTypeName(dvv::ParameterType::Colour) == "colour");
    CHECK(dvv::parameterTypeOf(dvv::ParameterValue{std::int64_t{1}}) == dvv::ParameterType::Int);
    CHECK(dvv::parameterTypeOf(dvv::ParameterValue{dvv::Colour{}}) == dvv::ParameterType::Colour);
}

TEST_CASE("duplicate names are rejected when the schema is built") {
    const auto error = buildError([](auto& builder) {
        builder.addBool("invert", "Invert", {}).addInt("invert", "Again", {});
    });
    CHECK(error == "parameter schema: duplicate parameter 'invert'");
}

TEST_CASE("invalid parameter declarations are rejected when the schema is built") {
    using Builder = dvv::ParameterSchema::Builder;
    constexpr double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();
    CHECK(buildError([](Builder& b) { b.addBool("", "Empty", {}); }) ==
          "parameter schema: invalid parameter name ''");
    CHECK(buildError([](Builder& b) { b.addBool("a=b", "Equals", {}); }) ==
          "parameter schema: invalid parameter name 'a=b'");
    CHECK(buildError([](Builder& b) {
              b.addInt("n", "N", {.min = 5, .max = 1, .defaultValue = 5});
          }) == "parameter schema: parameter 'n': min 5 is above max 1");
    CHECK(buildError([](Builder& b) {
              b.addInt("n", "N", {.min = 1, .max = 5, .defaultValue = 0});
          }) == "parameter schema: parameter 'n': 0 is outside [1, 5]");
    CHECK(buildError([](Builder& b) {
              b.addFloat("f", "F", {.min = NAN_VALUE, .max = 1.0, .defaultValue = 0.0});
          }) == "parameter schema: parameter 'f': min and max must be finite");
    CHECK(buildError([](Builder& b) {
              b.addFloat("f", "F", {.min = 0.0, .max = 1.0, .defaultValue = 2.0});
          }) == "parameter schema: parameter 'f': 2 is outside [0, 1]");
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {}, .defaultValue = ""});
          }) == "parameter schema: parameter 'e': an enum needs at least one option");
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a", ""}, .defaultValue = "a"});
          }) == "parameter schema: parameter 'e': empty enum option");
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a", "a"}, .defaultValue = "a"});
          }) == "parameter schema: parameter 'e': duplicate enum option 'a'");
    // parse() trims the CLI text, so such an option could never be selected.
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a", " b"}, .defaultValue = "a"});
          }) ==
          "parameter schema: parameter 'e': enum option ' b' starts or ends with white space");
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a\t", "b"}, .defaultValue = "b"});
          }) ==
          "parameter schema: parameter 'e': enum option 'a\t' starts or ends with white space");
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a b"}, .defaultValue = "a b"});
          }).empty());
    CHECK(buildError([](Builder& b) {
              b.addEnum("e", "E", {.options = {"a", "b"}, .defaultValue = "c"});
          }) == "parameter schema: parameter 'e': 'c' is not one of: a, b");
    CHECK(buildError([](Builder& b) {
              b.addRange("r", "R",
                         {.min = 0.0, .max = 1.0, .defaultValue = {.low = 0.8, .high = 0.2}});
          }) == "parameter schema: parameter 'r': low 0.8 is above high 0.2");
    CHECK(buildError([](Builder& b) { b.addColour("c", "C", {.defaultValue = {.r = 2.0F}}); }) ==
          "parameter schema: parameter 'c': colour components must be in [0, 1]");
    CHECK(buildError([](Builder& b) { b.addInt("n", "N", {}).addFloat("f", "F", {}); }).empty());
}

TEST_CASE("validate accepts partial values that fit the schema") {
    const auto schema = sampleSchema();
    CHECK(validateError(schema, {}).empty());
    CHECK(validateError(schema, schema.defaults()).empty());
    CHECK(validateError(schema, {{"radius", std::int64_t{10}}, {"mode", std::string("log")}})
              .empty());
    CHECK(validateError(schema, {{"levels", dvv::Range{.low = 0.0, .high = 0.0}}}).empty());
}

TEST_CASE("validate names the first problem") {
    const auto schema = sampleSchema();
    constexpr double INF = std::numeric_limits<double>::infinity();
    CHECK(validateError(schema, {{"size", std::int64_t{1}}}) == "unknown parameter 'size'");
    CHECK(validateError(schema, {{"gain", std::int64_t{1}}}) ==
          "parameter 'gain': expected float, got int");
    CHECK(validateError(schema, {{"mode", true}}) == "parameter 'mode': expected enum, got bool");
    CHECK(validateError(schema, {{"radius", std::int64_t{11}}}) ==
          "parameter 'radius': 11 is outside [1, 10]");
    CHECK(validateError(schema, {{"gain", -0.5}}) == "parameter 'gain': -0.5 is outside [0, 4]");
    CHECK(validateError(schema, {{"gain", INF}}) == "parameter 'gain': not a finite number");
    CHECK(validateError(schema, {{"mode", std::string("cubic")}}) ==
          "parameter 'mode': 'cubic' is not one of: linear, log");
    CHECK(validateError(schema, {{"levels", dvv::Range{.low = 0.5, .high = 0.4}}}) ==
          "parameter 'levels': low 0.5 is above high 0.4");
    CHECK(validateError(schema, {{"levels", dvv::Range{.low = -1.0, .high = 0.4}}}) ==
          "parameter 'levels': -1 is outside [0, 1]");
    CHECK(validateError(schema, {{"tint", dvv::Colour{.r = std::nanf("")}}}) ==
          "parameter 'tint': colour components must be in [0, 1]");
}

TEST_CASE("resolve fills in the defaults") {
    const auto schema = sampleSchema();
    const auto values = schema.resolve({{"radius", std::int64_t{7}}});
    REQUIRE(values.has_value());
    CHECK(values->size() == 6);
    CHECK(values->at("radius") == dvv::ParameterValue{std::int64_t{7}});
    CHECK(values->at("gain") == dvv::ParameterValue{1.0});

    const auto bad = schema.resolve({{"radius", std::int64_t{70}}});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == dvv::Errc::Format);
}
