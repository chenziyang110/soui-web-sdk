#pragma once
#include <windows.h>
#include <commdlg.h>
#include <json.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <cwchar>
#include <utility>

namespace scratch_file_save_detail {
enum class outcome { saved, cancelled, closing, dialog_failed, write_failed };

// Both exports accept data only. The user chooses the destination in native UI.
inline outcome save_file(HWND owner, const std::atomic_bool& closing, const wchar_t* name,
                         const wchar_t* extension, const wchar_t* filter, const wchar_t* title,
                         const void* data, std::size_t size) {
  if (closing.load(std::memory_order_acquire)) return outcome::closing;
  wchar_t chosen[32768]{};
  if (wcslen(name) >= 32768) return outcome::dialog_failed;
  wcscpy_s(chosen, name);
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
  dialog.lpstrFilter = filter; dialog.lpstrFile = chosen; dialog.nMaxFile = 32768;
  dialog.lpstrDefExt = extension; dialog.lpstrTitle = title;
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&dialog))
    return CommDlgExtendedError() ? outcome::dialog_failed : outcome::cancelled;
  if (closing.load(std::memory_order_acquire)) return outcome::closing;
  const auto temporary = std::wstring(chosen) + L".soui-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
  const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return outcome::write_failed;
  DWORD written = 0;
  const bool complete = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) &&
      written == size && FlushFileBuffers(file);
  const bool closed = CloseHandle(file) != FALSE;
  if (closing.load(std::memory_order_acquire)) {
    DeleteFileW(temporary.c_str()); return outcome::closing;
  }
  if (!complete || !closed || !MoveFileExW(temporary.c_str(), chosen, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str()); return outcome::write_failed;
  }
  return outcome::saved;
}

inline bool bounded_json_depth(std::string_view text, unsigned maximum) {
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (const char value : text) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (value == '\\') escaped = true;
      else if (value == '"') quoted = false;
    } else if (value == '"') quoted = true;
    else if (value == '{' || value == '[') { if (++depth > maximum) return false; }
    else if (value == '}' || value == ']') { if (!depth) return false; --depth; }
  }
  return !quoted && depth == 0;
}

struct metrics_payload { std::string text, error; };
inline metrics_payload read_metrics(std::string_view params) {
  using json = nlohmann::json;
  // The transport applies these limits too; retain them at this file boundary.
  if (params.size() > 32U * 1024U) return {{}, "E_METRICS_TOO_LARGE"};
  if (!bounded_json_depth(params, 4)) return {{}, "E_INVALID_METRICS_REQUEST"};
  const auto packet = json::parse(params, nullptr, false);
  if (!packet.is_object() || packet.size() != 1 || !packet.contains("report") || !packet["report"].is_string())
    return {{}, "E_INVALID_METRICS_REQUEST"};
  auto text = packet["report"].get<std::string>();
  if (text.size() > 24U * 1024U) return {{}, "E_METRICS_TOO_LARGE"};
  if (!bounded_json_depth(text, 16)) return {{}, "E_INVALID_METRICS_JSON"};
  const auto report = json::parse(text, nullptr, false);
  if (!report.is_object()) return {{}, "E_INVALID_METRICS_JSON"};
  if (!report.contains("schema") || !report["schema"].is_string() ||
      report["schema"].get<std::string>() != "soui.scratch-performance/v1")
    return {{}, "E_INVALID_METRICS_SCHEMA"};
  return {std::move(text), {}};
}
}  // namespace scratch_file_save_detail

// Demo-only bounded transaction: the page provides bytes, never a file path.
class scratch_project_export {
 public:
  static constexpr std::size_t maximum_bytes = 16U * 1024U * 1024U;
  std::string save_metrics(const soui::runtime::bridge_request& request, HWND owner, const std::atomic_bool& closing) {
    using json = nlohmann::json;
    const auto fail = [](const char* code) {
      return json{{"saved", false}, {"cancelled", false}, {"error", code}}.dump();
    };
    if (closing.load(std::memory_order_acquire)) return fail("E_APPLICATION_CLOSING");
    const auto payload = scratch_file_save_detail::read_metrics(request.params_json);
    if (!payload.error.empty()) return fail(payload.error.c_str());
    const auto result = scratch_file_save_detail::save_file(owner, closing, L"Scratch性能报告.json", L"json",
        L"Scratch 性能报告 (*.json)\0*.json\0\0", L"保存 Scratch 性能报告", payload.text.data(), payload.text.size());
    using outcome = scratch_file_save_detail::outcome;
    if (result == outcome::closing) return fail("E_APPLICATION_CLOSING");
    if (result == outcome::dialog_failed) return fail("E_SAVE_DIALOG");
    if (result == outcome::write_failed) return fail("E_METRICS_WRITE");
    return json{{"saved", result == outcome::saved}, {"cancelled", result == outcome::cancelled},
        {"bytes", payload.text.size()}}.dump();
  }
  std::string accept(const soui::runtime::bridge_request& request, HWND owner, const std::atomic_bool& closing) {
    using json = nlohmann::json;
    const auto fail = [this](const char* code) {
      reset(); return json{{"saved", false}, {"pending", false}, {"bytes", 0}, {"error", code}}.dump();
    };
    if (closing) return fail("E_APPLICATION_CLOSING");
    const auto packet = json::parse(request.params_json, nullptr, false);
    if (!packet.is_object() || packet.size() != 3 || !packet.contains("base64") ||
        !packet["base64"].is_string() || !packet.contains("offset") ||
        !packet["offset"].is_number_unsigned() || !packet.contains("final") || !packet["final"].is_boolean())
      return fail("E_INVALID_PROJECT_CHUNK");
    const auto offset = packet["offset"].get<std::uint64_t>();
    const auto encoded = packet["base64"].get<std::string>();
    if (encoded.size() > 24576 || offset > maximum_bytes) return fail("E_PROJECT_TOO_LARGE");
    std::vector<unsigned char> decoded;
    if (!decode(encoded, decoded)) return fail("E_INVALID_PROJECT_BASE64");
    if (offset == 0) { reset(); session_ = request.session_id; }
    if (session_ != request.session_id || offset != bytes_.size()) return fail("E_PROJECT_CHUNK_ORDER");
    if (decoded.size() > maximum_bytes - bytes_.size()) return fail("E_PROJECT_TOO_LARGE");
    bytes_.insert(bytes_.end(), decoded.begin(), decoded.end());
    if (!packet["final"].get<bool>())
      return json{{"saved", false}, {"pending", true}, {"bytes", bytes_.size()}}.dump();
    if (bytes_.size() < 4 || bytes_[0] != 'P' || bytes_[1] != 'K' || bytes_[2] != 3 || bytes_[3] != 4)
      return fail("E_INVALID_PROJECT_ZIP");
    const auto result = scratch_file_save_detail::save_file(owner, closing, L"我的作品.sb3", L"sb3",
        L"Scratch 项目 (*.sb3)\0*.sb3\0\0", L"保存 Scratch 项目", bytes_.data(), bytes_.size());
    using outcome = scratch_file_save_detail::outcome;
    if (result == outcome::closing) return fail("E_APPLICATION_CLOSING");
    if (result == outcome::dialog_failed) return fail("E_SAVE_DIALOG");
    if (result == outcome::write_failed) return fail("E_PROJECT_WRITE");
    const auto size = bytes_.size(); reset();
    return json{{"saved", result == outcome::saved}, {"cancelled", result == outcome::cancelled},
        {"pending", false}, {"bytes", size}}.dump();
  }
 private:
  static int sextet(unsigned char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
  }
  static bool decode(std::string_view text, std::vector<unsigned char>& output) {
    if (text.empty() || text.size() % 4) return false;
    output.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
      const int a = sextet(text[index]), b = sextet(text[index + 1]);
      const bool pad_c = text[index + 2] == '=', pad_d = text[index + 3] == '=';
      const int c = pad_c ? 0 : sextet(text[index + 2]), d = pad_d ? 0 : sextet(text[index + 3]);
      if (a < 0 || b < 0 || c < 0 || d < 0 || (pad_c && !pad_d) ||
          ((pad_c || pad_d) && index + 4 != text.size()) ||
          (pad_c && (b & 15)) || (!pad_c && pad_d && (c & 3))) return false;
      output.push_back(static_cast<unsigned char>((a << 2) | (b >> 4)));
      if (!pad_c) output.push_back(static_cast<unsigned char>((b << 4) | (c >> 2)));
      if (!pad_d) output.push_back(static_cast<unsigned char>((c << 6) | d));
    }
    return true;
  }
  void reset() { std::vector<unsigned char>{}.swap(bytes_); session_.clear(); }
  std::vector<unsigned char> bytes_;
  std::string session_;
};
