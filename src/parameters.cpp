#include "dvv/registry/parameters.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <system_error>
#include <type_traits>

namespace dvv {

namespace {

std::unexpected<Error> invalid(const Parameter& parameter, std::string_view problem) {
    return makeError(Errc::Format, std::format("parameter '{}': {}", parameter.name, problem));
}

bool isValidName(std::string_view name) noexcept {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '-';
    });
}

std::string joinOptions(const std::vector<std::string>& options) {
    std::string text;
    for (const auto& option : options) {
        if (!text.empty()) {
            text += ", ";
        }
        text += option;
    }
    return text;
}

template <class T>
Result<void> checkBounds(const Parameter& parameter, T number, T min, T max) {
    if (number < min || number > max) {
        return invalid(parameter, std::format("{} is outside [{}, {}]", number, min, max));
    }
    return {};
}

template <class T>
Result<void> checkLimits(const Parameter& parameter, T min, T max) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(min) || !std::isfinite(max)) {
            return invalid(parameter, "min and max must be finite");
        }
    }
    if (min > max) {
        return invalid(parameter, std::format("min {} is above max {}", min, max));
    }
    return {};
}

bool isUnitInterval(float component) noexcept {
    return component >= 0.0F && component <= 1.0F;  // false for NaN
}

// Checks a value of any type against one parameter.
Result<void> checkValue(const Parameter& parameter, const ParameterValue& value) {
    const ParameterType type = parameterTypeOf(value);
    if (type != parameter.type()) {
        return invalid(parameter,
                       std::format("expected {}, got {}", parameterTypeName(parameter.type()),
                                   parameterTypeName(type)));
    }
    switch (type) {
        case ParameterType::Int: {
            const auto& spec = std::get<IntParameter>(parameter.spec);
            return checkBounds(parameter, std::get<std::int64_t>(value), spec.min, spec.max);
        }
        case ParameterType::Float: {
            const auto& spec = std::get<FloatParameter>(parameter.spec);
            const double number = std::get<double>(value);
            if (!std::isfinite(number)) {
                return invalid(parameter, "not a finite number");
            }
            return checkBounds(parameter, number, spec.min, spec.max);
        }
        case ParameterType::Bool: return {};
        case ParameterType::Enum: {
            const auto& spec = std::get<EnumParameter>(parameter.spec);
            const auto& text = std::get<std::string>(value);
            if (std::ranges::find(spec.options, text) == spec.options.end()) {
                return invalid(parameter, std::format("'{}' is not one of: {}", text,
                                                      joinOptions(spec.options)));
            }
            return {};
        }
        case ParameterType::Range: {
            const auto& spec = std::get<RangeParameter>(parameter.spec);
            const auto& range = std::get<Range>(value);
            if (!std::isfinite(range.low) || !std::isfinite(range.high)) {
                return invalid(parameter, "not a finite number");
            }
            if (range.low > range.high) {
                return invalid(parameter,
                               std::format("low {} is above high {}", range.low, range.high));
            }
            DVV_TRY_VOID(checkBounds(parameter, range.low, spec.min, spec.max));
            return checkBounds(parameter, range.high, spec.min, spec.max);
        }
        case ParameterType::Colour: {
            const auto& colour = std::get<Colour>(value);
            if (!isUnitInterval(colour.r) || !isUnitInterval(colour.g) ||
                !isUnitInterval(colour.b) || !isUnitInterval(colour.a)) {
                return invalid(parameter, "colour components must be in [0, 1]");
            }
            return {};
        }
    }
    return invalid(parameter, "unknown parameter type");
}

std::string_view trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// Checks a declaration: its limits or options, then its default.
Result<void> checkDeclaration(const Parameter& parameter) {
    switch (parameter.type()) {
        case ParameterType::Int: {
            const auto& spec = std::get<IntParameter>(parameter.spec);
            DVV_TRY_VOID(checkLimits(parameter, spec.min, spec.max));
            break;
        }
        case ParameterType::Float: {
            const auto& spec = std::get<FloatParameter>(parameter.spec);
            DVV_TRY_VOID(checkLimits(parameter, spec.min, spec.max));
            break;
        }
        case ParameterType::Range: {
            const auto& spec = std::get<RangeParameter>(parameter.spec);
            DVV_TRY_VOID(checkLimits(parameter, spec.min, spec.max));
            break;
        }
        case ParameterType::Enum: {
            const auto& options = std::get<EnumParameter>(parameter.spec).options;
            if (options.empty()) {
                return invalid(parameter, "an enum needs at least one option");
            }
            for (auto option = options.begin(); option != options.end(); ++option) {
                if (option->empty()) {
                    return invalid(parameter, "empty enum option");
                }
                // parse() trims the CLI text, so such an option could never be selected.
                if (trim(*option) != *option) {
                    return invalid(
                        parameter,
                        std::format("enum option '{}' starts or ends with white space", *option));
                }
                if (std::find(std::next(option), options.end(), *option) != options.end()) {
                    return invalid(parameter, std::format("duplicate enum option '{}'", *option));
                }
            }
            break;
        }
        case ParameterType::Bool:
        case ParameterType::Colour: break;
    }
    return checkValue(parameter, parameter.defaultValue());
}

// The whole text (spaces around it ignored) as one number, or nothing.
template <class T>
std::optional<T> readNumber(std::string_view text) noexcept {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    T number{};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [stop, status] = std::from_chars(begin, end, number);
    if (status != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return number;
}

// Comma-separated numbers; nothing when a field is not a number.
template <class T>
std::optional<std::vector<T>> readNumbers(std::string_view text) {
    std::vector<T> numbers;
    for (const auto field : std::views::split(text, ',')) {
        const auto number = readNumber<T>(std::string_view(field.begin(), field.end()));
        if (!number) {
            return std::nullopt;
        }
        numbers.push_back(*number);
    }
    return numbers;
}

// A JSON number as a colour component; NaN (rejected by checkValue) when it does not fit a float.
float toComponent(const json::Value& number) {
    const auto wide = number.get<double>();
    return std::fabs(wide) <= std::numeric_limits<float>::max()
               ? static_cast<float>(wide)
               : std::numeric_limits<float>::quiet_NaN();
}

// The double with the float's shortest decimal form, so JSON shows 0.1 for 0.1F rather than
// 0.10000000149011612; reading it back as a float gives the same float.
double toJsonComponent(float component) {
    const std::string text = std::format("{}", component);
    double number = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
}

// The CLI text of a value for one parameter, before checkValue.
Result<ParameterValue> readText(const Parameter& parameter, std::string_view text) {
    switch (parameter.type()) {
        case ParameterType::Int:
            if (const auto number = readNumber<std::int64_t>(text)) {
                return *number;
            }
            return invalid(parameter, std::format("'{}' is not an integer", text));
        case ParameterType::Float:
            if (const auto number = readNumber<double>(text)) {
                return *number;
            }
            return invalid(parameter, std::format("'{}' is not a number", text));
        case ParameterType::Bool: {
            const std::string_view word = trim(text);
            if (word == "true" || word == "1") {
                return true;
            }
            if (word == "false" || word == "0") {
                return false;
            }
            return invalid(parameter,
                           std::format("'{}' is not a boolean (true, false, 1 or 0)", text));
        }
        case ParameterType::Enum: return std::string(trim(text));
        case ParameterType::Range:
            if (const auto numbers = readNumbers<double>(text); numbers && numbers->size() == 2) {
                return Range{.low = (*numbers)[0], .high = (*numbers)[1]};
            }
            return invalid(parameter, std::format("'{}' is not a range (low,high)", text));
        case ParameterType::Colour:
            if (const auto numbers = readNumbers<float>(text);
                numbers && (numbers->size() == 3 || numbers->size() == 4)) {
                return Colour{.r = (*numbers)[0],
                              .g = (*numbers)[1],
                              .b = (*numbers)[2],
                              .a = numbers->size() == 4 ? (*numbers)[3] : 1.0F};
            }
            return invalid(parameter, std::format("'{}' is not a colour (r,g,b or r,g,b,a)", text));
    }
    return invalid(parameter, "unknown parameter type");
}

// The JSON form of a value for one parameter, before checkValue.
Result<ParameterValue> readJson(const Parameter& parameter, const json::Value& value) {
    const auto mismatch = [&](std::string_view expected) {
        return invalid(parameter, std::format("expected {}, got {}", expected, json::dump(value)));
    };
    const auto is_number_array = [&](std::size_t size) {
        return value.is_array() && value.size() == size &&
               std::all_of(value.begin(), value.end(),
                           [](const json::Value& item) { return item.is_number(); });
    };
    switch (parameter.type()) {
        case ParameterType::Int:
            // nlohmann/json keeps non-negative integers as unsigned; they must fit int64.
            if (!value.is_number_integer() ||
                (value.is_number_unsigned() &&
                 !std::in_range<std::int64_t>(value.get<std::uint64_t>()))) {
                return mismatch("an integer");
            }
            return value.get<std::int64_t>();
        case ParameterType::Float:
            if (!value.is_number()) {
                return mismatch("a number");
            }
            return value.get<double>();
        case ParameterType::Bool:
            if (!value.is_boolean()) {
                return mismatch("a boolean");
            }
            return value.get<bool>();
        case ParameterType::Enum:
            if (!value.is_string()) {
                return mismatch("a string");
            }
            return value.get<std::string>();
        case ParameterType::Range:
            if (!is_number_array(2)) {
                return mismatch("[low, high]");
            }
            return Range{.low = value[0].get<double>(), .high = value[1].get<double>()};
        case ParameterType::Colour:
            if (!is_number_array(3) && !is_number_array(4)) {
                return mismatch("[r, g, b] or [r, g, b, a]");
            }
            return Colour{.r = toComponent(value[0]),
                          .g = toComponent(value[1]),
                          .b = toComponent(value[2]),
                          .a = value.size() == 4 ? toComponent(value[3]) : 1.0F};
    }
    return invalid(parameter, "unknown parameter type");
}

}  // namespace

std::string_view parameterTypeName(ParameterType type) noexcept {
    switch (type) {
        case ParameterType::Int: return "int";
        case ParameterType::Float: return "float";
        case ParameterType::Bool: return "bool";
        case ParameterType::Enum: return "enum";
        case ParameterType::Range: return "range";
        case ParameterType::Colour: return "colour";
    }
    return "unknown";
}

ParameterType parameterTypeOf(const ParameterValue& value) noexcept {
    return static_cast<ParameterType>(value.index());
}

ParameterValue Parameter::defaultValue() const {
    return std::visit([](const auto& typed) { return ParameterValue{typed.defaultValue}; }, spec);
}

const Parameter* ParameterSchema::find(std::string_view name) const noexcept {
    const auto found = std::ranges::find(parameters_, name, &Parameter::name);
    return found != parameters_.end() ? &*found : nullptr;
}

ParameterValues ParameterSchema::defaults() const {
    ParameterValues values;
    for (const auto& parameter : parameters_) {
        values.emplace(parameter.name, parameter.defaultValue());
    }
    return values;
}

Result<void> ParameterSchema::validate(const ParameterValues& values) const {
    for (const auto& [name, value] : values) {
        const Parameter* parameter = find(name);
        if (parameter == nullptr) {
            return makeError(Errc::Format, std::format("unknown parameter '{}'", name));
        }
        DVV_TRY_VOID(checkValue(*parameter, value));
    }
    return {};
}

Result<ParameterValues> ParameterSchema::resolve(const ParameterValues& values) const {
    DVV_TRY_VOID(validate(values));
    ParameterValues resolved = defaults();
    for (const auto& [name, value] : values) {
        resolved.insert_or_assign(name, value);
    }
    return resolved;
}

ParameterSchema::Builder& ParameterSchema::Builder::addInt(std::string name, std::string label,
                                                           IntParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), spec});
    return *this;
}

ParameterSchema::Builder& ParameterSchema::Builder::addFloat(std::string name, std::string label,
                                                             FloatParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), spec});
    return *this;
}

ParameterSchema::Builder& ParameterSchema::Builder::addBool(std::string name, std::string label,
                                                            BoolParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), spec});
    return *this;
}

ParameterSchema::Builder& ParameterSchema::Builder::addEnum(std::string name, std::string label,
                                                            EnumParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), std::move(spec)});
    return *this;
}

ParameterSchema::Builder& ParameterSchema::Builder::addRange(std::string name, std::string label,
                                                             RangeParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), spec});
    return *this;
}

ParameterSchema::Builder& ParameterSchema::Builder::addColour(std::string name, std::string label,
                                                              ColourParameter spec) {
    parameters_.push_back(Parameter{std::move(name), std::move(label), spec});
    return *this;
}

Result<ParameterSchema> ParameterSchema::Builder::build() const {
    const auto fail = [](std::string problem) {
        return makeError(Errc::Internal, "parameter schema: " + std::move(problem));
    };
    for (auto parameter = parameters_.begin(); parameter != parameters_.end(); ++parameter) {
        if (!isValidName(parameter->name)) {
            return fail(std::format("invalid parameter name '{}'", parameter->name));
        }
        if (std::find_if(parameters_.begin(), parameter, [&](const Parameter& earlier) {
                return earlier.name == parameter->name;
            }) != parameter) {
            return fail(std::format("duplicate parameter '{}'", parameter->name));
        }
        if (auto checked = checkDeclaration(*parameter); !checked) {
            return fail(std::move(checked.error().message));
        }
    }
    return ParameterSchema(parameters_);
}

Result<ParameterValue> ParameterSchema::parse(std::string_view name, std::string_view text) const {
    const Parameter* parameter = find(name);
    if (parameter == nullptr) {
        return makeError(Errc::Format, std::format("unknown parameter '{}'", name));
    }
    DVV_TRY(ParameterValue value, readText(*parameter, text));
    DVV_TRY_VOID(checkValue(*parameter, value));
    return value;
}

std::string ParameterSchema::format(const ParameterValue& value) {
    return std::visit(
        [](const auto& typed) -> std::string {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, bool>) {
                return typed ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return typed;
            } else if constexpr (std::is_same_v<T, Range>) {
                return std::format("{},{}", typed.low, typed.high);
            } else if constexpr (std::is_same_v<T, Colour>) {
                return std::format("{},{},{},{}", typed.r, typed.g, typed.b, typed.a);
            } else {
                return std::format("{}", typed);
            }
        },
        value);
}

json::Value ParameterSchema::toJson(const ParameterValues& values) {
    json::Value object = json::Value::object();
    for (const auto& [name, value] : values) {
        object[name] = std::visit(
            [](const auto& typed) -> json::Value {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, Range>) {
                    return json::Value::array({typed.low, typed.high});
                } else if constexpr (std::is_same_v<T, Colour>) {
                    return json::Value::array({toJsonComponent(typed.r), toJsonComponent(typed.g),
                                               toJsonComponent(typed.b), toJsonComponent(typed.a)});
                } else {
                    return json::Value(typed);
                }
            },
            value);
    }
    return object;
}

Result<ParameterValues> ParameterSchema::fromJson(const json::Value& object) const {
    if (!object.is_object()) {
        return makeError(Errc::Format, std::format("parameters: expected a JSON object, got {}",
                                                   json::dump(object)));
    }
    ParameterValues values;
    for (auto item = object.begin(); item != object.end(); ++item) {
        const Parameter* parameter = find(item.key());
        if (parameter == nullptr) {
            return makeError(Errc::Format, std::format("unknown parameter '{}'", item.key()));
        }
        DVV_TRY(ParameterValue value, readJson(*parameter, item.value()));
        DVV_TRY_VOID(checkValue(*parameter, value));
        values.emplace(item.key(), std::move(value));
    }
    return values;
}

}  // namespace dvv
