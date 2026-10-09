#include <soui/host/runtime_profile.h>

#include <windows.h>

namespace soui::sdk {
namespace {
const char* architecture(WORD value) noexcept {
  switch (value) {
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
    default: return "unknown";
  }
}
runtime_selection reject(runtime_profile profile, const char* code, const char* reason) {
  return {false, profile, renderer_backend::none, true, code, reason};
}
}

runtime_environment inspect_runtime_environment() {
  runtime_environment result;
#if defined(_M_X64)
  result.process_architecture = "x64";
#elif defined(_M_IX86)
  result.process_architecture = "x86";
#else
  result.process_architecture = "unknown";
#endif
  SYSTEM_INFO system{};
  GetNativeSystemInfo(&system);
  result.native_architecture = architecture(system.wProcessorArchitecture);
  // Query the real version without relying on an application manifest's
  // compatibility declarations. No post-Win7 static imports are introduced.
  using rtl_get_version = LONG (WINAPI*)(OSVERSIONINFOW*);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto query = ntdll ? reinterpret_cast<rtl_get_version>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (query && query(&version) == 0) {
    result.os_version_known = true;
    result.os_major = version.dwMajorVersion;
    result.os_minor = version.dwMinorVersion;
    result.os_build = version.dwBuildNumber;
  }
  HKEY key{};
  const auto opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",
                                  0, KEY_READ, &key);
  if (opened == ERROR_SUCCESS) {
    result.pe_known = result.windows_pe = true;
    RegCloseKey(key);
  } else if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) {
    result.pe_known = true;
  }
  result.remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
  return result;
}

runtime_selection select_runtime_profile(const runtime_environment& environment,
    renderer_availability available, runtime_profile requested, bool requires_modern_web) {
  if (requested != runtime_profile::automatic && requested != runtime_profile::compatibility &&
      requested != runtime_profile::modern && requested != runtime_profile::pe)
    return reject(requested, "E_INVALID_PROFILE", "Unknown runtime profile.");
  if (!environment.os_version_known || !environment.pe_known)
    return reject(requested, "E_ENVIRONMENT_UNKNOWN", "Cannot establish the OS version or PE environment.");
  if (environment.os_major < 6 || (environment.os_major == 6 && environment.os_minor < 1))
    return reject(requested, "E_OS_VERSION", "Windows 7 or a newer compatible Windows environment is required.");
  if ((environment.process_architecture != "x86" && environment.process_architecture != "x64") ||
      (environment.native_architecture != "x86" && environment.native_architecture != "x64"))
    return reject(requested, "E_ARCHITECTURE", "This delivery supports x86/x64; other architectures need separate validation.");
  if (environment.process_architecture == "x64" && environment.native_architecture != "x64")
    return reject(requested, "E_ARCHITECTURE", "A 64-bit application requires a 64-bit OS.");
  if (environment.windows_pe && environment.process_architecture != environment.native_architecture)
    return reject(requested, "E_PE_ARCHITECTURE", "PE requires an application matching the native image architecture.");
  if (requested == runtime_profile::pe && !environment.windows_pe)
    return reject(requested, "E_NOT_PE", "The PE profile requires an observed PE environment.");
  if (environment.windows_pe && (requires_modern_web || requested == runtime_profile::modern))
    return reject(requested, "E_PE_MODERN_UNVERIFIED", "Modern WebView2 on PE is not an admitted backend; use a verified PE-compatible application.");
  const bool modern_os = environment.os_major >= 10;
  if (requires_modern_web || requested == runtime_profile::modern) {
    if (requested == runtime_profile::compatibility || requested == runtime_profile::pe)
      return reject(requested, "E_PROFILE_CONFLICT", "This application requires modern Web capabilities unavailable in the requested profile.");
    if (!modern_os)
      return reject(requested, "E_MODERN_OS", "The modern backend requires Windows 10 or later; frozen legacy WebView2 is not selected.");
    if (!available.webview2)
      return reject(requested, "E_WEBVIEW2_UNAVAILABLE", "Install a supported WebView2 Runtime or configure a compatible fixed runtime.");
    return {true, runtime_profile::modern, renderer_backend::webview2, false, {},
            "Modern candidate selected; controller interfaces and application features must still be verified."};
  }
  if (requested == runtime_profile::automatic && !environment.windows_pe && modern_os && available.webview2)
    return {true, runtime_profile::modern, renderer_backend::webview2, false, {},
            "Modern candidate selected before startup; runtime failure will not silently change backends."};
  if (!available.miniblink)
    return reject(requested, "E_COMPATIBILITY_UNAVAILABLE", "The application-local compatibility runtime is missing.");
  return {true, environment.windows_pe ? runtime_profile::pe : runtime_profile::compatibility,
          renderer_backend::miniblink, true, {},
          "Compatibility software presentation selected; actual image support requires target validation."};
}

const char* profile_name(runtime_profile value) noexcept {
  switch (value) {
    case runtime_profile::automatic: return "auto";
    case runtime_profile::compatibility: return "compatibility";
    case runtime_profile::modern: return "modern";
    case runtime_profile::pe: return "pe";
    default: return "unknown";
  }
}
const char* backend_name(renderer_backend value) noexcept {
  switch (value) {
    case renderer_backend::none: return "none";
    case renderer_backend::miniblink: return "miniblink";
    case renderer_backend::webview2: return "webview2";
    default: return "unknown";
  }
}
}  // namespace soui::sdk
