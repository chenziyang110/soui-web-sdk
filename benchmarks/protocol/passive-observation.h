#pragma once
// Private CPU-only contracts for InputTrace's explicit passive mode.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace soui::benchmark {
struct passive_error : std::runtime_error { using std::runtime_error::runtime_error; };
inline void passive_require(bool value, const char* reason) { if (!value) throw passive_error(reason); }
struct passive_options {
  std::uint32_t pid{};
  std::uint64_t creation{}, hwnd{};
  std::string run;
  unsigned interval_ms{}, duration_ms{}, startup_ms{};
  std::filesystem::path output;
};
inline std::uint64_t passive_integer(const wchar_t* text, std::uint64_t maximum) {
  if (!text || !*text || (*text == L'0' && text[1])) throw std::runtime_error("Invalid passive integer");
  std::uint64_t value{};
  for (; *text; ++text) {
    if (*text < L'0' || *text > L'9') throw std::runtime_error("Invalid passive integer");
    const auto digit = static_cast<unsigned>(*text - L'0');
    if (value > (maximum - digit) / 10) throw std::runtime_error("Passive integer exceeds limit");
    value = value * 10 + digit;
  }
  if (!value) throw std::runtime_error("Passive integer must be positive");
  return value;
}
// Relative waitable-timer due time, rounded up without unsigned overflow.
// A conversion outside the signed 100ns range fails closed, never arms early.
inline bool passive_relative_due_100ns(std::uint64_t ticks, std::uint64_t frequency, std::int64_t& due) noexcept {
  due = 0;
  if (!ticks || !frequency) return false;
  constexpr std::uint64_t units = 10000000;
  constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const auto seconds = ticks / frequency, remainder = ticks % frequency;
  if (seconds > limit / units || remainder > std::numeric_limits<std::uint64_t>::max() / units) return false;
  const auto scaled = remainder * units;
  const auto fraction = scaled / frequency + (scaled % frequency != 0);
  const auto whole = seconds * units;
  if (fraction > limit - whole) return false;
  const auto total = whole + fraction;
  if (!total) return false;
  due = -static_cast<std::int64_t>(total);
  return true;
}
inline passive_options parse_passive_options(int argc, wchar_t** argv) {
  if (argc != 10 || !argv || !argv[1] || std::wstring(argv[1]) != L"--observe-manual")
    throw std::runtime_error("Usage: InputTrace --observe-manual PID CREATION HWND RUN INTERVAL_MS DURATION_MS STARTUP_MS ABS_NEW_OUTPUT");
  passive_options result;
  result.pid = static_cast<std::uint32_t>(passive_integer(argv[2], std::numeric_limits<std::uint32_t>::max()));
  result.creation = passive_integer(argv[3], std::numeric_limits<std::uint64_t>::max());
  result.hwnd = passive_integer(argv[4], std::numeric_limits<std::uintptr_t>::max());
  if (!argv[5] || std::wstring(argv[5]).size() != 32) throw std::runtime_error("Passive run must be 32 lowercase hex digits");
  bool nonzero{};
  for (const auto c : std::wstring(argv[5])) {
    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) throw std::runtime_error("Invalid passive run");
    result.run += static_cast<char>(c); nonzero = nonzero || c != L'0';
  }
  if (!nonzero) throw std::runtime_error("Passive run must be nonzero");
  result.interval_ms = static_cast<unsigned>(passive_integer(argv[6], 1000));
  result.duration_ms = static_cast<unsigned>(passive_integer(argv[7], 120000));
  result.startup_ms = static_cast<unsigned>(passive_integer(argv[8], 60000));
  if (result.duration_ms < 1000 || result.startup_ms < 1000) throw std::runtime_error("Passive duration/startup must be at least 1000 ms");
  if (!argv[9] || std::wstring(argv[9]).size() > 32760) throw std::runtime_error("Invalid passive output path");
  result.output = std::filesystem::path(argv[9]);
  if (!result.output.is_absolute() || result.output.filename().empty()) throw std::runtime_error("Passive output must be an absolute new file");
  return result;
}
inline std::uint32_t passive_nonce(const std::string& run) {
  std::uint32_t value{};
  for (std::size_t n = 24; n < 32; ++n) value = (value << 4) | static_cast<unsigned>(run[n] <= '9' ? run[n] - '0' : run[n] - 'a' + 10);
  return value ? value : 1;
}
inline std::uint16_t passive_crc16(const std::array<unsigned char, 16>& data) {
  std::uint16_t value = 0xffff;
  for (unsigned n = 0; n < data.size(); ++n) {
    if (n == 12 || n == 13) continue;
    value ^= static_cast<std::uint16_t>(data[n]) << 8;
    for (unsigned b = 0; b < 8; ++b) value = static_cast<std::uint16_t>((value << 1) ^ ((value & 0x8000) ? 0x1021 : 0));
  }
  return value;
}
inline std::string passive_hex(std::uint32_t value, unsigned digits) {
  std::string result(digits, '0'); constexpr char alphabet[] = "0123456789abcdef";
  for (unsigned n = 0; n < digits; ++n) { result[digits - n - 1] = alphabet[value & 15]; value >>= 4; }
  return result;
}
struct passive_pixels {
  std::array<unsigned char, 16> bytes{};
  std::array<unsigned char, 3> rgb{};
  std::uint32_t nonce{}, revision{}, frame{}, crc32{};
  unsigned flags{}, epoch{};
  bool marker_readable{};
  const char* reason{};
  std::string marker_hex() const {
    std::string result; for (const auto byte : bytes) result += passive_hex(byte, 2); return result;
  }
};
// The copied ROI is [8,328,64,40]; marker y=8 and original-content probe (2,2).
// CRC32 is a native fingerprint of all BGRA bytes, excluding row-pitch padding.
inline passive_pixels decode_passive_pixels(const void* data, std::size_t pitch, std::uint32_t nonce) {
  if (!data || pitch < 64 * 4 || pitch > 1024 * 1024) throw std::runtime_error("capture-failed");
  passive_pixels result;
  const auto* pixels = static_cast<const unsigned char*>(data);
  const auto* probe = pixels + 2 * pitch + 2 * 4;
  result.rgb = {probe[2], probe[1], probe[0]};
  std::uint32_t crc32 = 0xffffffff;
  for (unsigned y = 0; y < 40; ++y) for (unsigned x = 0; x < 64 * 4; ++x) {
    crc32 ^= pixels[y * pitch + x];
    for (unsigned b = 0; b < 8; ++b) crc32 = (crc32 >> 1) ^ ((crc32 & 1) ? 0xedb88320U : 0);
  }
  result.crc32 = ~crc32;
  for (unsigned i = 0; i < 128; ++i) {
    const auto* pixel = pixels + (8 + (i / 16) * 4 + 2) * pitch + ((i % 16) * 4 + 2) * 4;
    const bool dark = pixel[0] <= 32 && pixel[1] <= 32 && pixel[2] <= 32;
    const bool light = pixel[0] >= 223 && pixel[1] >= 223 && pixel[2] >= 223;
    if (!dark && !light) { result.reason = "marker-pixels"; return result; }
    if (light) result.bytes[i / 8] |= static_cast<unsigned char>(1U << (i % 8));
  }
  result.marker_readable = true;
  const auto little = [&result](unsigned start, unsigned count) {
    std::uint32_t value{}; for (unsigned n = 0; n < count; ++n) value |= static_cast<std::uint32_t>(result.bytes[start + n]) << (n * 8); return value;
  };
  result.nonce = little(2, 4); result.revision = little(6, 3); result.frame = little(9, 2);
  result.flags = result.bytes[11]; result.epoch = little(14, 2);
  if (result.bytes[0] != 0xa7 || result.bytes[1] != 0x4d) result.reason = "marker-magic";
  else if (passive_crc16(result.bytes) != little(12, 2)) result.reason = "marker-crc";
  else if (result.revision >= (1U << 20) || !result.frame) result.reason = "marker-counter";
  else if (result.flags) result.reason = "marker-flags";
  else if (result.nonce != nonce) result.reason = "marker-nonce";
  else if (!result.epoch) result.reason = "marker-epoch";
  else if (result.rgb != std::array<unsigned char, 3>{static_cast<unsigned char>(73 * result.revision), static_cast<unsigned char>(151 * result.revision), static_cast<unsigned char>(29 * result.revision)}) result.reason = "probe-mismatch";
  return result;
}
struct passive_transition {
  // frame_gap counts unobserved page render() sequence values. They can be
  // coalesced before one desktop update; this is not a lost-desktop-frame count.
  std::uint32_t revision_delta{}, frame_gap{};
  bool content_change{}, marker_only{}, duplicate{};
  const char* reason{};
};
struct passive_sequence {
  passive_pixels previous{};
  bool initialized{};
  passive_transition accept(const passive_pixels& value) {
    if (value.reason) throw std::runtime_error("Invalid passive sequence input");
    passive_transition result;
    if (initialized) {
      if (value.epoch != previous.epoch) throw std::runtime_error("epoch-changed");
      if (value.nonce != previous.nonce || value.frame < previous.frame || value.revision < previous.revision ||
          (value.frame == previous.frame && value.revision != previous.revision)) throw std::runtime_error("counter-regressed");
      result.revision_delta = value.revision - previous.revision;
      result.frame_gap = value.frame > previous.frame ? value.frame - previous.frame - 1 : 0;
      const bool same_content = value.rgb == previous.rgb;
      result.marker_only = value.bytes != previous.bytes && same_content;
      result.duplicate = value.bytes == previous.bytes && same_content;
      result.content_change = result.revision_delta && !same_content;
      if (result.revision_delta && same_content) result.reason = "content-alias";
    }
    previous = value; initialized = true;
    return result;
  }
};
} // namespace soui::benchmark
