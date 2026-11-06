#pragma once

#include "bridge_pending_script.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace soui::sdk::detail {
namespace bridge_source_detail {

// Inputs never refer to the fresh destination string. Reserve for the common
// unescaped size; uncommon escaped data grows normally instead of reserving 6x.
inline void reserve(std::string& destination, std::size_t fixed,
                    std::string_view first, std::string_view second = {}) {
  const auto limit = destination.max_size();
  if (fixed > limit || first.size() > limit - fixed ||
      second.size() > limit - fixed - first.size())
    throw std::length_error("Bridge script source exceeds string capacity.");
  destination.reserve(fixed + first.size() + second.size());
}

inline void append_literal(std::string& destination, std::string_view value) {
  destination.push_back('"');
  std::size_t run = 0;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto character = static_cast<unsigned char>(value[index]);
    if (character >= 0x20U && character != '\\' && character != '"') continue;
    if (index != run) destination.append(value.data() + run, index - run);
    switch (character) {
      case '\\': destination += "\\\\"; break;
      case '"': destination += "\\\""; break;
      case '\r': destination += "\\r"; break;
      case '\n': destination += "\\n"; break;
      case '\t': destination += "\\t"; break;
      default: {
        static constexpr char hex[] = "0123456789abcdef";
        destination += "\\u00";
        destination.push_back(hex[(character >> 4U) & 0x0FU]);
        destination.push_back(hex[character & 0x0FU]);
        break;
      }
    }
    run = index + 1;
  }
  if (run != value.size()) destination.append(value.data() + run, value.size() - run);
  destination.push_back('"');
}

}  // namespace bridge_source_detail

inline std::string bridge_json_string_literal(std::string_view value) {
  std::string result;
  bridge_source_detail::reserve(result, 2U, value);
  bridge_source_detail::append_literal(result, value);
  return result;
}

inline std::string bridge_pending_source(std::string_view request_id,
                                         std::uint32_t timeout, bool renewal) {
  const auto timeout_text = std::to_string(timeout);
  const std::string_view renewal_text = renewal ? "true" : "false";
  std::string result;
  // "return " + function + '(' + quoted ID + ',' + timeout + ',' + bool + ");"
  constexpr auto fixed = 7U + sizeof(create_bridge_pending_script) - 1U + 1U + 2U + 1U + 1U + 2U;
  bridge_source_detail::reserve(result, fixed + timeout_text.size() + renewal_text.size(), request_id);
  result.append("return ");
  result.append(create_bridge_pending_script, sizeof(create_bridge_pending_script) - 1U);
  result.push_back('(');
  bridge_source_detail::append_literal(result, request_id);
  result.push_back(',');
  result.append(timeout_text);
  result.push_back(',');
  result.append(renewal_text.data(), renewal_text.size());
  result.append(");");
  return result;
}

inline std::string bridge_resolution_source(std::string_view request_id,
                                            std::string_view response,
                                            bool return_statement = false) {
  static constexpr char prefix[] = "(function(){var p=window.__souiBridgePending;var id=";
  static constexpr char middle[] = ";if(!p||!p[id])return;return p[id].finish(";
  static constexpr char suffix[] = ");})()";
  constexpr auto fixed = sizeof(prefix) - 1U + sizeof(middle) - 1U + sizeof(suffix) - 1U + 4U;
  std::string result;
  bridge_source_detail::reserve(result, fixed + (return_statement ? 8U : 0U), request_id, response);
  if (return_statement) result.append("return ");
  result.append(prefix, sizeof(prefix) - 1U);
  bridge_source_detail::append_literal(result, request_id);
  result.append(middle, sizeof(middle) - 1U);
  bridge_source_detail::append_literal(result, response);
  result.append(suffix, sizeof(suffix) - 1U);
  if (return_statement) result.push_back(';');
  return result;
}

}  // namespace soui::sdk::detail
