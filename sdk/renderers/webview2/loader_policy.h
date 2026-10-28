#pragma once

#include <soui/host/webview_host.h>
#include <windows.h>
#include <shobjidl.h>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace soui::sdk::detail {
namespace webview2_loader_policy {

// Existence/metadata only. No inherited argument, policy body, or application
// identity is included in diagnostics or copied to an audit record.
inline host_result failure(const char* code) {
  return {false, code, "WebView2 loader configuration could not be admitted."};
}
inline host_result overrides_from_environment() {
  constexpr const wchar_t* names[]{L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", L"WEBVIEW2_USER_DATA_FOLDER",
      L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"WEBVIEW2_CHANNEL_SEARCH_KIND", L"WEBVIEW2_RELEASE_CHANNELS",
      L"WEBVIEW2_WAIT_FOR_SCRIPT_DEBUGGER", L"WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGER"};
  for (const auto name : names) {
    SetLastError(ERROR_SUCCESS);
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    const auto error = GetLastError();
    if (size || error == ERROR_SUCCESS) return failure("E_WEBVIEW2_LOADER_OVERRIDE");
    if (error != ERROR_ENVVAR_NOT_FOUND) return failure("E_WEBVIEW2_LOADER_POLICY");
  }
  return {true, {}, {}};
}

constexpr DWORD maximum_values = 256;
constexpr DWORD maximum_selector = 1024;
constexpr DWORD maximum_image_path = 32768;
struct metadata final { DWORD values{}, longest_name{}; FILETIME written{}; };
inline LSTATUS query_metadata(HKEY key, metadata& output) {
  return RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      &output.values, &output.longest_name, nullptr, nullptr, &output.written);
}
struct policy_key final {
  HKEY handle{};
  bool wildcard{};
  metadata before;
};
class keys final {
 public:
  ~keys() { close(); }
  keys() = default;
  keys(const keys&) = delete;
  keys& operator=(const keys&) = delete;
  host_result finish(host_result result) {
    if (!close()) return failure("E_WEBVIEW2_LOADER_POLICY");
    return result;
  }
  std::array<policy_key, 20> values;
 private:
  bool close() noexcept {
    bool success = true;
    for (auto& key : values) if (key.handle) {
      const auto handle = key.handle; key.handle = nullptr;
      if (RegCloseKey(handle) != ERROR_SUCCESS) success = false;
    }
    return success;
  }
};

inline bool copy_identity(const wchar_t* text, std::wstring& output) {
  if (!text) return false;
  std::size_t count{};
  while (count <= maximum_selector && text[count]) ++count;
  if (!count || count > maximum_selector) return false;
  output.assign(text, count);
  return true;
}
inline bool application_identities(std::vector<std::wstring>& output) {
  bool complete = true;
  using query_identity = LONG(WINAPI*)(UINT32*, PWSTR);
  const auto module = GetModuleHandleW(L"kernel32.dll");
  const auto query = module ? reinterpret_cast<query_identity>(
      GetProcAddress(module, "GetCurrentApplicationUserModelId")) : nullptr;
  if (!query) complete = false; // Win7 compatible import; unavailable is not an invented identity.
  else {
    UINT32 length{};
    auto result = query(&length, nullptr);
    // This status excludes only this API's identity. It says nothing about
    // the independently assigned Shell process explicit AppUserModelID.
    if (result != APPMODEL_ERROR_NO_APPLICATION) {
      if (result != ERROR_INSUFFICIENT_BUFFER || length < 2 || length > maximum_selector + 1) complete = false;
      else {
        std::vector<wchar_t> buffer(length, L'\0');
        const auto capacity = length;
        result = query(&length, buffer.data());
        if (result == APPMODEL_ERROR_NO_APPLICATION) { /* Only this API is absent. */ }
        else if (result != ERROR_SUCCESS || length < 2 || length > capacity || buffer[length - 1] != L'\0') complete = false;
        else {
          std::wstring identity;
          if (!copy_identity(buffer.data(), identity) || identity.size() + 1 != length) complete = false;
          else output.push_back(std::move(identity));
        }
      }
    }
  }
  struct shell_identity final {
    PWSTR value{};
    ~shell_identity() { CoTaskMemFree(value); }
  } explicit_id;
  const auto result = GetCurrentProcessExplicitAppUserModelID(&explicit_id.value);
  std::wstring identity;
  if (result != S_OK || !copy_identity(explicit_id.value, identity)) complete = false;
  else output.push_back(std::move(identity));
  // Both successful IDs are candidates. No undocumented precedence between
  // these distinct APIs is assumed; E_FAIL is not documented as "unset".
  return complete;
}
inline bool executable_selector(std::wstring& output) {
  std::vector<wchar_t> path(maximum_image_path, L'\0');
  SetLastError(ERROR_SUCCESS);
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!count || count >= path.size()) return false;
  const std::wstring image(path.data(), count);
  const auto separator = image.find_last_of(L"\\/");
  const auto start = separator == std::wstring::npos ? 0 : separator + 1;
  if (start == image.size() || image.size() - start > maximum_selector) return false;
  output.assign(image, start, std::wstring::npos);
  return true;
}
inline host_result selector_status(HKEY key, const wchar_t* selector) {
  DWORD type{}, bytes{};
  // lpData is always null: even an empty/malformed value is conservatively
  // rejected without reading its body or attempting to simulate precedence.
  const auto result = RegQueryValueExW(key, selector, nullptr, &type, nullptr, &bytes);
  if (result == ERROR_SUCCESS || result == ERROR_MORE_DATA) return failure("E_WEBVIEW2_LOADER_OVERRIDE");
  if (result != ERROR_FILE_NOT_FOUND) return failure("E_WEBVIEW2_LOADER_POLICY");
  return {true, {}, {}};
}
} // namespace webview2_loader_policy

inline host_result check_webview2_loader_policy() {
  namespace policy = webview2_loader_policy;
  auto accepted = policy::overrides_from_environment();
  if (!accepted.ok) return accepted;
  struct option final { const wchar_t* name; bool wildcard; };
  constexpr option options[]{{L"BrowserExecutableFolder", true}, {L"ChannelSearchKind", true},
      {L"ReleaseChannels", true}, {L"AdditionalBrowserArguments", true}, {L"UserDataFolder", false}};
  const HKEY hives[]{HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
  constexpr REGSAM views[]{KEY_WOW64_32KEY, KEY_WOW64_64KEY};
  policy::keys opened;
  bool any_values = false;
  std::size_t index{};
  for (const auto hive : hives) for (const auto view : views) for (const auto& option : options) {
    auto& key = opened.values[index++]; key.wildcard = option.wildcard;
    const auto path = std::wstring(L"Software\\Policies\\Microsoft\\Edge\\WebView2\\") + option.name;
    HKEY candidate{};
    const auto status = RegOpenKeyExW(hive, path.c_str(), 0, KEY_QUERY_VALUE | view, &candidate);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) continue;
    if (status != ERROR_SUCCESS) return opened.finish(policy::failure("E_WEBVIEW2_LOADER_POLICY"));
    key.handle = candidate; // Adopt only a successful RegOpenKeyEx result.
    if (!key.handle || policy::query_metadata(key.handle, key.before) != ERROR_SUCCESS)
      return opened.finish(policy::failure("E_WEBVIEW2_LOADER_POLICY"));
    if (key.before.values > policy::maximum_values || key.before.longest_name > policy::maximum_selector)
      return opened.finish(policy::failure("E_WEBVIEW2_LOADER_POLICY"));
    any_values = any_values || key.before.values != 0;
  }
  // No AUMID resolution is needed only after every applicable key was actually
  // opened/queried or proved absent in both hives and both registry views.
  if (!any_values) return opened.finish({true, {}, {}});
  std::vector<std::wstring> selectors;
  const bool application_ids_known = policy::application_identities(selectors);
  std::wstring executable;
  const bool executable_known = policy::executable_selector(executable);
  if (!application_ids_known || !executable_known)
    return opened.finish(policy::failure("E_POLICY_ID_UNRESOLVED"));
  selectors.push_back(std::move(executable));
  for (const auto& key : opened.values) {
    if (!key.handle) continue;
    for (const auto& selector : selectors) {
      accepted = policy::selector_status(key.handle, selector.c_str());
      if (!accepted.ok) return opened.finish(std::move(accepted));
    }
    if (key.wildcard) {
      accepted = policy::selector_status(key.handle, L"*");
      if (!accepted.ok) return opened.finish(std::move(accepted));
    }
    policy::metadata after;
    if (policy::query_metadata(key.handle, after) != ERROR_SUCCESS ||
        after.values != key.before.values || after.longest_name != key.before.longest_name ||
        after.written.dwLowDateTime != key.before.written.dwLowDateTime ||
        after.written.dwHighDateTime != key.before.written.dwHighDateTime)
      return opened.finish(policy::failure("E_WEBVIEW2_LOADER_POLICY"));
  }
  return opened.finish({true, {}, {}});
}
} // namespace soui::sdk::detail
