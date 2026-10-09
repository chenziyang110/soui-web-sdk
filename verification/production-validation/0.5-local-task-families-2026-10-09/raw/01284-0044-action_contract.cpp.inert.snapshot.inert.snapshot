#include <soui/runtime/bridge/action_contract.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <utility>

#include "../../../../third_party/include/json/json.hpp"

namespace soui::runtime {
namespace {
using json = nlohmann::json;
constexpr double max_safe_integer = 9007199254740991.0;

bool unicode_length(std::string_view text, std::size_t& length) {
  length = 0;
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    std::uint32_t code = lead;
    unsigned remaining = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80U) {}
    else if (lead >= 0xc2U && lead <= 0xdfU) { code = lead & 0x1fU; remaining = 1; minimum = 0x80U; }
    else if (lead >= 0xe0U && lead <= 0xefU) { code = lead & 0x0fU; remaining = 2; minimum = 0x800U; }
    else if (lead >= 0xf0U && lead <= 0xf4U) { code = lead & 0x07U; remaining = 3; minimum = 0x10000U; }
    else return false;
    if (remaining > text.size() - i) return false;
    while (remaining--) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0U) != 0x80U) return false;
      code = (code << 6U) | (next & 0x3fU);
    }
    if (code < minimum || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
    ++length;
  }
  return true;
}

bool valid_text(const json& value, std::size_t min_bytes, std::size_t max_bytes) {
  if (!value.is_string()) return false;
  const auto& text = value.get_ref<const std::string&>();
  std::size_t unused = 0;
  return text.size() >= min_bytes && text.size() <= max_bytes && unicode_length(text, unused);
}

json parse_bounded(std::string_view text, std::size_t max_bytes, std::size_t max_depth) {
  if (text.empty() || text.size() > max_bytes) return json();
  return json::parse(text.begin(), text.end(), [max_depth](int depth, json::parse_event_t, json&) {
    if (static_cast<std::size_t>(depth) > max_depth) throw std::invalid_argument("JSON depth exceeds the action bound");
    return true;
  });
}

std::size_t depth(const json& value) {
  std::size_t result = 1;
  if (value.is_structured())
    for (const auto& item : value) result = std::max(result, 1U + depth(item));
  return result;
}

bool only_keys(const json& value, std::initializer_list<const char*> allowed) {
  if (!value.is_object()) return false;
  for (const auto& field : value.items())
    if (std::none_of(allowed.begin(), allowed.end(), [&](const char* key) { return field.key() == key; })) return false;
  return true;
}

bool safe_number(const json& value, double& number) {
  if (!value.is_number()) return false;
  number = value.get<double>();
  return std::isfinite(number) && std::abs(number) <= max_safe_integer;
}

bool bound(const json& value, std::size_t maximum, std::size_t& number) {
  double candidate = 0;
  if (!safe_number(value, candidate) || candidate < 0 || std::floor(candidate) != candidate || candidate > maximum) return false;
  number = static_cast<std::size_t>(candidate);
  return true;
}

bool length_range(const json& schema, const char* minimum_key, const char* maximum_key,
                  std::size_t maximum, std::size_t& lower, std::size_t& upper) {
  lower = 0;
  return schema.contains(maximum_key) && bound(schema[maximum_key], maximum, upper) &&
      (!schema.contains(minimum_key) || bound(schema[minimum_key], maximum, lower)) && lower <= upper;
}

bool valid_schema(const json& schema) {
  if (!schema.is_object() || !schema.contains("type") || !schema["type"].is_string()) return false;
  const auto type = schema["type"].get<std::string>();
  if (type == "object") {
    if (!only_keys(schema, {"type", "properties", "required", "additionalProperties"}) ||
        !schema.contains("properties") || !schema["properties"].is_object() ||
        schema["properties"].size() > action_contract::max_properties ||
        !schema.contains("additionalProperties") || schema["additionalProperties"] != false) return false;
    const auto& properties = schema["properties"];
    for (const auto& property : properties.items()) {
      std::size_t unused = 0;
      if (property.key().empty() || property.key().size() > 128U || !unicode_length(property.key(), unused) ||
          !valid_schema(property.value())) return false;
    }
    if (schema.contains("required")) {
      const auto& required = schema["required"];
      if (!required.is_array() || required.size() > action_contract::max_properties) return false;
      std::set<std::string> seen;
      for (const auto& item : required)
        if (!valid_text(item, 1U, 128U) || !properties.contains(item.get<std::string>()) ||
            !seen.insert(item.get<std::string>()).second) return false;
    }
    return true;
  }
  if (type == "array") {
    std::size_t minimum = 0, maximum = 0;
    return only_keys(schema, {"type", "items", "minItems", "maxItems"}) && schema.contains("items") &&
        valid_schema(schema["items"]) && length_range(schema, "minItems", "maxItems", action_contract::max_items, minimum, maximum);
  }
  if (type == "string") {
    std::size_t minimum = 0, maximum = 0;
    if (!only_keys(schema, {"type", "minLength", "maxLength", "enum"}) ||
        !length_range(schema, "minLength", "maxLength", action_contract::max_string_length, minimum, maximum)) return false;
    if (schema.contains("enum")) {
      const auto& values = schema["enum"];
      if (!values.is_array() || values.empty() || values.size() > action_contract::max_properties) return false;
      std::set<std::string> seen;
      for (const auto& item : values)
        if (!valid_text(item, 0U, action_contract::max_schema_bytes) || !seen.insert(item.get<std::string>()).second) return false;
    }
    return true;
  }
  if (type == "integer" || type == "number") {
    if (!only_keys(schema, {"type", "minimum", "maximum"})) return false;
    double minimum = -max_safe_integer, maximum = max_safe_integer;
    if ((schema.contains("minimum") && !safe_number(schema["minimum"], minimum)) ||
        (schema.contains("maximum") && !safe_number(schema["maximum"], maximum))) return false;
    return minimum <= maximum;
  }
  return (type == "boolean" || type == "null") && only_keys(schema, {"type"});
}

bool matches(const json& schema, const json& value) {
  const auto type = schema["type"].get<std::string>();
  if (type == "object") {
    if (!value.is_object() || value.size() > action_contract::max_properties) return false;
    const auto& properties = schema["properties"];
    if (schema.contains("required"))
      for (const auto& required : schema["required"])
        if (!value.contains(required.get<std::string>())) return false;
    for (const auto& property : value.items()) {
      const auto found = properties.find(property.key());
      if (found == properties.end() || !matches(*found, property.value())) return false;
    }
    return true;
  }
  if (type == "array") {
    if (!value.is_array() || value.size() < schema.value("minItems", std::size_t{0}) ||
        value.size() > schema["maxItems"].get<std::size_t>()) return false;
    return std::all_of(value.begin(), value.end(), [&](const json& item) { return matches(schema["items"], item); });
  }
  if (type == "string") {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    std::size_t length = 0;
    if (!unicode_length(text, length) || length < schema.value("minLength", std::size_t{0}) ||
        length > schema["maxLength"].get<std::size_t>()) return false;
    return !schema.contains("enum") || std::any_of(schema["enum"].begin(), schema["enum"].end(),
        [&](const json& item) { return item == value; });
  }
  if (type == "boolean") return value.is_boolean();
  if (type == "null") return value.is_null();
  double number = 0;
  if (!safe_number(value, number) || (type == "integer" && std::floor(number) != number)) return false;
  return number >= schema.value("minimum", -max_safe_integer) && number <= schema.value("maximum", max_safe_integer);
}

bool valid_value(const json& schema, std::string_view text) {
  try {
    // The caller still enforces the complete Bridge response/request limits.
    const auto value = parse_bounded(text, 64U * 1024U, action_contract::max_depth);
    return depth(value) <= action_contract::max_depth && matches(schema, value);
  } catch (const json::exception&) { return false; }
  catch (const std::invalid_argument&) { return false; }
}
}  // namespace

struct action_contract::impl {
  json input;
  json output;
  std::string encoded;
};

action_contract::action_contract(std::shared_ptr<const impl> state) : state_{std::move(state)} {}

std::shared_ptr<const action_contract> action_contract::parse(std::string_view text) {
  try {
    const auto descriptor = parse_bounded(text, max_contract_bytes, max_depth + 2U);
    if (descriptor.size() != 5U || !only_keys(descriptor, {"protocol", "title", "description", "inputSchema", "outputSchema"}) ||
        descriptor.value("protocol", std::string{}) != "soui.action/1" ||
        !valid_text(descriptor["title"], 1U, 256U) || !valid_text(descriptor["description"], 0U, 1024U)) return nullptr;
    for (const char* key : {"inputSchema", "outputSchema"}) {
      const auto& schema = descriptor[key];
      if (!schema.is_object() || schema.value("type", std::string{}) != "object" ||
          depth(schema) > max_depth || schema.dump().size() > max_schema_bytes || !valid_schema(schema)) return nullptr;
    }
    const auto encoded = descriptor.dump();
    if (encoded.size() > max_contract_bytes) return nullptr;
    auto state = std::make_shared<impl>(impl{descriptor["inputSchema"], descriptor["outputSchema"], encoded});
    return std::shared_ptr<const action_contract>(new action_contract(std::move(state)));
  } catch (const json::exception&) { return nullptr; }
  catch (const std::invalid_argument&) { return nullptr; }
}

bool action_contract::validates_input(std::string_view text) const { return valid_value(state_->input, text); }
bool action_contract::validates_output(std::string_view text) const { return valid_value(state_->output, text); }
const std::string& action_contract::encoded() const noexcept { return state_->encoded; }

}  // namespace soui::runtime
