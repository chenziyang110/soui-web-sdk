#include <soui/desktop/app.h>

#include <algorithm>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace soui::desktop {
namespace {
// Bound allocations even when platform APIs report unusually long paths.
constexpr std::size_t maximum_path_units = 64 * 1024;

std::filesystem::path executable_path() {
#if defined(_WIN32)
  for (std::size_t capacity = 256; capacity <= maximum_path_units; capacity *= 2) {
    std::vector<wchar_t> buffer(capacity);
    const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return {};
    if (length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length));
  }
#elif defined(__APPLE__)
  std::vector<char> buffer(256);
  auto capacity = static_cast<std::uint32_t>(buffer.size());
  if (::_NSGetExecutablePath(buffer.data(), &capacity) != 0) {
    if (capacity == 0 || capacity > maximum_path_units) return {};
    buffer.resize(capacity);
    if (::_NSGetExecutablePath(buffer.data(), &capacity) != 0) return {};
  }
  const auto end = std::find(buffer.begin(), buffer.end(), '\0');
  if (end == buffer.end()) return {};
  return std::filesystem::path(std::string(buffer.begin(), end));
#elif defined(__linux__)
  for (std::size_t capacity = 256; capacity <= maximum_path_units; capacity *= 2) {
    std::vector<char> buffer(capacity);
    const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length < 0) return {};
    if (static_cast<std::size_t>(length) < buffer.size())
      return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length)));
  }
#endif
  return {};
}

path_result path_failure(const char* code, const char* message) {
  return {result::failure(code, message), {}};
}
}  // namespace

path_result application_resource_directory() {
  try {
    const auto located = executable_path();
    // Never interpret a failed/relative platform result against the caller's cwd.
    if (located.empty() || !located.is_absolute())
      return path_failure("executable_path_unavailable", "Cannot locate the running application executable.");
    std::error_code error;
    const auto executable = std::filesystem::canonical(located, error);
    if (error || !std::filesystem::is_regular_file(executable, error) || error)
      return path_failure("executable_path_unavailable", "Cannot resolve the running application executable.");
    auto base = executable.parent_path();
#if defined(__APPLE__)
    if (base.filename() == "MacOS" && base.parent_path().filename() == "Contents")
      base = base.parent_path() / "Resources";
#endif
    const auto resources = std::filesystem::canonical(base / "web", error);
    if (error || !std::filesystem::is_directory(resources, error) || error)
      return path_failure("resource_directory_unavailable", "The application web resource directory is missing or inaccessible.");
    return {result::success(), resources};
  } catch (...) {
    return path_failure("resource_directory_unavailable", "Cannot resolve the application resource directory.");
  }
}
}  // namespace soui::desktop
