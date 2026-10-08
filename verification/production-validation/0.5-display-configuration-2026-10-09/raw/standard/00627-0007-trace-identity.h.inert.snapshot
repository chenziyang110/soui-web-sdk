#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace soui::benchmark {
enum class identity_schema { legacy_v1, exact_v2 };

inline std::uint64_t parse_expected_filetime(const wchar_t* text) {
  if (!text || *text < L'1' || *text > L'9')
    throw std::runtime_error("Expected creation FILETIME must be canonical positive decimal uint64");
  std::uint64_t value = 0;
  for (const wchar_t* next = text; *next; ++next) {
    if (*next < L'0' || *next > L'9') throw std::runtime_error("Invalid expected creation FILETIME");
    const auto digit = static_cast<unsigned>(*next - L'0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
      throw std::runtime_error("Expected creation FILETIME exceeds uint64");
    value = value * 10U + digit;
  }
  return value;
}

inline std::string encode_filetime(std::uint64_t value, identity_schema schema = identity_schema::exact_v2) {
  const auto decimal = std::to_string(value);
  return schema == identity_schema::exact_v2 ? '"' + decimal + '"' : decimal;
}

inline std::string expected_field(std::optional<std::uint64_t> expected, identity_schema schema) {
  if (schema == identity_schema::legacy_v1) return {};
  return ",\"expectedCreationTime100ns\":" + (expected ? encode_filetime(*expected) : "null");
}

inline std::string process_begin(std::uint32_t pid, std::uint64_t creation, std::uint64_t started,
    std::uint32_t interval, std::uint32_t duration, std::optional<std::uint64_t> expected = {},
    identity_schema schema = identity_schema::exact_v2) {
  return "{\"event\":\"run.begin\",\"schema\":\"soui.process-tree-samples/" +
      std::string(schema == identity_schema::exact_v2 ? "v2" : "v1") + "\",\"rootPid\":" +
      std::to_string(pid) + ",\"rootCreationTime100ns\":" + encode_filetime(creation, schema) +
      ",\"startedAtUtcFiletime100ns\":" + encode_filetime(started, schema) + expected_field(expected, schema) +
      ",\"intervalMs\":" + std::to_string(interval) + ",\"durationMs\":" + std::to_string(duration) + "}\n";
}

inline std::string input_begin(std::uint32_t pid, const std::string& scene, const std::string& mode,
    std::uint64_t creation, std::uint64_t started, std::uint64_t frequency,
    std::optional<std::uint64_t> expected = {}, identity_schema schema = identity_schema::exact_v2) {
  if ((scene != "S01" && scene != "S02" && scene != "S03" && scene != "S04" && scene != "S05") ||
      (mode != "full" && mode != "probe")) throw std::runtime_error("Invalid input identity scene or mode");
  return "{\"event\":\"run.begin\",\"schema\":\"soui.external-input-trace/" +
      std::string(schema == identity_schema::exact_v2 ? "v2" : "v1") + "\",\"pid\":" + std::to_string(pid) +
      ",\"scene\":\"" + scene + "\",\"contentWidth\":1100,\"contentHeight\":720" +
      ",\"processCreationTime100ns\":" + encode_filetime(creation, schema) +
      ",\"qpcFrequency\":" + std::to_string(frequency) +
      ",\"startedAtUtcFiletime100ns\":" + encode_filetime(started, schema) + expected_field(expected, schema) +
      ",\"mode\":\"" + mode + "\"}\n";
}
}  // namespace soui::benchmark
