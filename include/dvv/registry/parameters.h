#pragma once

// Component parameters (spec §9.2): a ParameterSchema describes them for the GUI (property grid),
// the CLI (--param name=value, parse/format) and session files (toJson/fromJson);
// ParameterValues carries the chosen values.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/json.h"

namespace dvv {

// A low/high pair, e.g. black and white levels.
struct Range {
    double low = 0.0;
    double high = 0.0;

    friend bool operator==(const Range&, const Range&) = default;
};

// RGBA, each component in [0, 1].
struct Colour {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    friend bool operator==(const Colour&, const Colour&) = default;
};

// One value; an enum value is the option's name.
using ParameterValue = std::variant<std::int64_t, double, bool, std::string, Range, Colour>;

// Parameter name -> value, sorted by name.
using ParameterValues = std::map<std::string, ParameterValue, std::less<>>;

// Same order as the alternatives of ParameterValue and Parameter::spec.
enum class ParameterType : std::uint8_t { Int, Float, Bool, Enum, Range, Colour };

[[nodiscard]] std::string_view parameterTypeName(ParameterType type) noexcept;
[[nodiscard]] ParameterType parameterTypeOf(const ParameterValue& value) noexcept;

struct IntParameter {
    std::int64_t min = std::numeric_limits<std::int64_t>::min();
    std::int64_t max = std::numeric_limits<std::int64_t>::max();
    std::int64_t defaultValue = 0;
};

// min, max and every value are finite.
struct FloatParameter {
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
    double defaultValue = 0.0;
};

struct BoolParameter {
    bool defaultValue = false;
};

// Options are non-empty, unique and without white space at either end (parse() trims the text);
// the default is one of them.
struct EnumParameter {
    std::vector<std::string> options;
    std::string defaultValue;
};

// Both ends lie in [min, max] and low <= high.
struct RangeParameter {
    double min = 0.0;
    double max = 1.0;
    Range defaultValue{.low = 0.0, .high = 1.0};
};

struct ColourParameter {
    Colour defaultValue;
};

struct Parameter {
    std::string name;   // key in ParameterValues, the CLI and session files: [A-Za-z0-9_.-]+
    std::string label;  // for people
    std::variant<IntParameter, FloatParameter, BoolParameter, EnumParameter, RangeParameter,
                 ColourParameter>
        spec;

    [[nodiscard]] ParameterType type() const noexcept {
        return static_cast<ParameterType>(spec.index());
    }
    [[nodiscard]] ParameterValue defaultValue() const;
};

namespace detail {

// True when ParameterType Type indexes Value in ParameterValue and Spec in Parameter::spec.
template <ParameterType Type, class Value, class Spec>
consteval bool indexesAlternatives() {
    constexpr auto INDEX = static_cast<std::size_t>(Type);
    return std::is_same_v<std::variant_alternative_t<INDEX, ParameterValue>, Value> &&
           std::is_same_v<std::variant_alternative_t<INDEX, decltype(Parameter::spec)>, Spec>;
}

}  // namespace detail

// Parameter::type() and parameterTypeOf() cast a variant index to ParameterType.
static_assert(std::variant_size_v<ParameterValue> ==
                      static_cast<std::size_t>(ParameterType::Colour) + 1 &&
                  std::variant_size_v<decltype(Parameter::spec)> ==
                      static_cast<std::size_t>(ParameterType::Colour) + 1,
              "every alternative of ParameterValue and Parameter::spec needs a ParameterType");
static_assert(detail::indexesAlternatives<ParameterType::Int, std::int64_t, IntParameter>() &&
                  detail::indexesAlternatives<ParameterType::Float, double, FloatParameter>() &&
                  detail::indexesAlternatives<ParameterType::Bool, bool, BoolParameter>() &&
                  detail::indexesAlternatives<ParameterType::Enum, std::string, EnumParameter>() &&
                  detail::indexesAlternatives<ParameterType::Range, Range, RangeParameter>() &&
                  detail::indexesAlternatives<ParameterType::Colour, Colour, ColourParameter>(),
              "ParameterType must follow the order of ParameterValue and Parameter::spec");

// Immutable description of a component's parameters, built with ParameterSchema::Builder.
class ParameterSchema {
public:
    class Builder;

    // No parameters.
    ParameterSchema() = default;

    [[nodiscard]] std::span<const Parameter> parameters() const noexcept { return parameters_; }
    [[nodiscard]] const Parameter* find(std::string_view name) const noexcept;

    [[nodiscard]] ParameterValues defaults() const;

    // Errc::Format naming the first problem in name order: an unknown name, a wrong type, a value
    // out of range or an enum value that is not an option. Missing names are allowed.
    [[nodiscard]] Result<void> validate(const ParameterValues& values) const;

    // validate(), then the defaults for every missing name.
    [[nodiscard]] Result<ParameterValues> resolve(const ParameterValues& values) const;

    // Reads the text of CLI "--param name=value": int as a decimal integer, float as a decimal
    // number (an exponent is allowed), bool as true, false, 1 or 0, enum as the option's name,
    // range as "low,high" and colour as "r,g,b" or "r,g,b,a" (alpha 1 when omitted); spaces
    // around the commas are allowed. Errc::Format for an unknown name, unreadable text or a value
    // that validate() rejects.
    [[nodiscard]] Result<ParameterValue> parse(std::string_view name, std::string_view text) const;

    // The text parse() reads back as the same value; floating-point numbers in their shortest
    // exact form. For values validate() accepts.
    [[nodiscard]] static std::string format(const ParameterValue& value);

    // JSON object for session files: numbers, booleans and strings as themselves, a range as
    // [low, high] and a colour as [r, g, b, a]. For values validate() accepts; NaN or infinity
    // would become null.
    [[nodiscard]] static json::Value toJson(const ParameterValues& values);

    // Reads toJson()'s form; integers are accepted for float parameters. Errc::Format for a
    // non-object, an unknown name, a value of the wrong JSON type or one that validate() rejects.
    [[nodiscard]] Result<ParameterValues> fromJson(const json::Value& object) const;

private:
    explicit ParameterSchema(std::vector<Parameter> parameters) noexcept
        : parameters_(std::move(parameters)) {}

    std::vector<Parameter> parameters_;
};

// Collects parameter declarations; build() checks them. Invalid declarations (bad or duplicate
// names, min above max, a default that does not fit) are programming errors: build() returns
// Errc::Internal naming the first one.
class ParameterSchema::Builder {
public:
    Builder& addInt(std::string name, std::string label, IntParameter spec);
    Builder& addFloat(std::string name, std::string label, FloatParameter spec);
    Builder& addBool(std::string name, std::string label, BoolParameter spec);
    Builder& addEnum(std::string name, std::string label, EnumParameter spec);
    Builder& addRange(std::string name, std::string label, RangeParameter spec);
    Builder& addColour(std::string name, std::string label, ColourParameter spec);

    [[nodiscard]] Result<ParameterSchema> build() const;

private:
    std::vector<Parameter> parameters_;
};

}  // namespace dvv
