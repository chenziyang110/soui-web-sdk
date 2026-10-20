'use strict';

// Static native scripts: Node is needed only while staging, never by the user.
// Discovery is deliberately weaker than loader/ABI/native-runtime verification.
function generatePreflight({ platform, arch, minimumOSVersion = '12.0' }) {
  if (!['linux', 'windows', 'macos'].includes(platform) ||
      !['x86', 'x64', 'arm32', 'arm64'].includes(arch)) throw new Error('Unsupported preflight target.');
  if (platform === 'macos' && !/^\d{1,5}\.\d{1,5}(?:\.\d{1,5})?$/.test(minimumOSVersion))
    throw new Error('Invalid minimum macOS version.');
  if (platform === 'windows') return { filename: 'doctor.ps1', mode: 0o644, content: windows(arch) };
  return { filename: 'doctor.sh', mode: 0o755, content: platform === 'linux' ? linux(arch) : macos(arch, minimumOSVersion) };
}
const shellHeader = `#!/bin/sh
# Read-only diagnostic; no target execution, installation, or persistent logs.
status=0
report() { printf '%s %s %s\\n' "$1" "$2" "$3"; }
`;
function linux(arch) {
  return shellHeader + `host_platform=$(uname -s 2>/dev/null) || host_platform=
case "$host_platform" in
  Linux) :;;
  '') report UNKNOWN PLATFORM_LINUX 'Platform discovery unavailable; verify this is the declared Linux target.';;
  *) report MISSING PLATFORM_LINUX 'Run this application on its declared Linux target.'; exit 1;;
esac
case "$(uname -m 2>/dev/null)" in
  x86_64|amd64) host_arch=x64;; i?86) host_arch=x86;; aarch64|arm64) host_arch=arm64;; arm*) host_arch=arm32;; *) host_arch=unknown;;
esac
if [ "$host_arch" = '${arch}' ]; then
  report PASS ARCH_NATIVE 'Kernel architecture matches; userspace ABI remains unverified.'
else
  report UNKNOWN ARCH_COMPATIBILITY 'Check application architecture, userspace ABI and any required emulation.'
fi
if [ -z "\${DISPLAY:-}" ] && [ -z "\${WAYLAND_DISPLAY:-}" ]; then
  report UNKNOWN DISPLAY_ENV 'No explicit display variable; default Wayland or backend connections may still work. If startup fails, use an active graphical desktop session.'
else
  report PASS DISPLAY_ENV 'Display variable present; compositor connectivity is not verified.'
fi
cache_available=0
cache=
if [ -x /sbin/ldconfig ]; then
  if cache=$(/sbin/ldconfig -p 2>/dev/null); then cache_available=1; fi
elif [ -x /usr/sbin/ldconfig ]; then
  if cache=$(/usr/sbin/ldconfig -p 2>/dev/null); then cache_available=1; fi
fi
check_library() {
  if [ "$cache_available" -eq 1 ] && printf '%s\\n' "$cache" | grep -F "$1" >/dev/null 2>&1; then
    report PASS "$2" 'Shared-library cache contains a candidate; architecture, ABI and loader resolution are unverified.'
  else
    report UNKNOWN "$2" 'No cache candidate established; verify official distro runtime packages and application dependency deployment.'
  fi
}
check_library libgtk-3.so.0 GTK_LIBRARY_CANDIDATE
check_library libwebkit2gtk-4.1.so.0 WEBKIT_LIBRARY_CANDIDATE
check_library libjavascriptcoregtk-4.1.so.0 JAVASCRIPTCORE_LIBRARY_CANDIDATE
check_helper() {
  found=0
  for candidate in /usr/lib/webkit2gtk-4.1/"$1" /usr/lib64/webkit2gtk-4.1/"$1" /usr/lib/*/webkit2gtk-4.1/"$1" /usr/libexec/webkit2gtk-4.1/"$1"; do
    if [ -f "$candidate" ] && [ -x "$candidate" ]; then found=1; break; fi
  done
  if [ "$found" -eq 1 ]; then
    report PASS "$2" 'Executable helper candidate found; configured WebKit helper path and loading are unverified.'
  else
    report UNKNOWN "$2" 'No executable helper found in standard locations; verify the official full WebKitGTK runtime, not only development headers.'
  fi
}
check_helper WebKitNetworkProcess WEBKIT_NETWORK_HELPER_CANDIDATE
check_helper WebKitWebProcess WEBKIT_WEB_HELPER_CANDIDATE
report UNKNOWN TRANSITIVE_DEPENDENCIES 'Additional shared libraries, graphics, sandbox availability and actual WebView startup require target verification.'
report INFO NEXT_STEP 'Use the distribution package manager documentation for GTK 3.24+ and WebKitGTK 4.1 2.40+; this tool installs nothing.'
report INFO RESULT 'Exit zero means no known blocker, not successful application or browser startup.'
exit "$status"
`;
}
function macos(arch, minimum) {
  const parts = minimum.split('.');
  return shellHeader + `host_platform=$(uname -s 2>/dev/null) || host_platform=
case "$host_platform" in
  Darwin) :;;
  '') report UNKNOWN PLATFORM_MACOS 'Platform discovery unavailable; verify this is the declared macOS target.';;
  *) report MISSING PLATFORM_MACOS 'Run this application on its declared macOS target.'; exit 1;;
esac
case "$(uname -m 2>/dev/null)" in x86_64) host_arch=x64;; arm64) host_arch=arm64;; *) host_arch=unknown;; esac
if [ "$host_arch" = '${arch}' ]; then
  report PASS ARCH_NATIVE 'Reported execution architecture matches the application.'
else
  report UNKNOWN ARCH_COMPATIBILITY 'Check native architecture and any required Rosetta support; emulation is not tested.'
fi
version=$(/usr/bin/sw_vers -productVersion 2>/dev/null) || version=
case "$version" in
  ''|*[!0-9.]*) report UNKNOWN MACOS_VERSION 'System version could not be established; verify the application minimum macOS requirement.';;
  *)
    old_ifs=$IFS; IFS=.; set -- $version; IFS=$old_ifs
    major=\${1:-0}; minor=\${2:-0}; patch=\${3:-0}
    case "$major:$minor:$patch" in *::*|:*|*:) report UNKNOWN MACOS_VERSION 'System version components are unavailable.';;
      *)
        if [ "$major" -gt ${Number(parts[0])} ] || { [ "$major" -eq ${Number(parts[0])} ] && [ "$minor" -gt ${Number(parts[1])} ]; } || { [ "$major" -eq ${Number(parts[0])} ] && [ "$minor" -eq ${Number(parts[1])} ] && [ "$patch" -ge ${Number(parts[2] || 0)} ]; }; then
          report PASS MACOS_VERSION 'The reported OS meets the declared application minimum.'
        else
          report MISSING MACOS_VERSION 'Use a supported macOS version or obtain an application build for this system.'
          status=1
        fi;;
    esac;;
esac
report UNKNOWN SYSTEM_FRAMEWORKS 'AppKit, Foundation and WebKit are OS frameworks and may reside in dyld cache; independent file existence is not a runtime test.'
report UNKNOWN APP_STARTUP 'Additional libraries, code signing, notarization and graphical startup remain unverified.'
report INFO NEXT_STEP 'Use the application vendor deployment instructions; this tool does not alter Gatekeeper or install software.'
report INFO RESULT 'Exit zero means no known blocker, not successful application or browser startup.'
exit "$status"
`;
}
function windows(arch) {
  // Microsoft documents pv in the EdgeUpdate client key for Evergreen detection:
  // https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution
  return `# Read-only diagnostic. Do not bypass PowerShell execution policy to run it.
$ErrorActionPreference = 'Stop'
function Report([string]$State, [string]$Code, [string]$Message) { Write-Output "$State $Code $Message" }
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
  Report MISSING PLATFORM_WINDOWS 'Run this application on its declared Windows target.'
  exit 1
}
try {
  $HostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
  if ($HostArchitecture -eq '${arch}') { Report PASS ARCH_NATIVE 'Operating system architecture matches; binary loading remains unverified.' }
  else { Report UNKNOWN ARCH_COMPATIBILITY 'Check application architecture and Windows emulation support.' }
} catch { Report UNKNOWN ARCH_COMPATIBILITY 'Operating system architecture could not be established.' }
$Found = $false
foreach ($Hive in @([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryHive]::CurrentUser)) {
  foreach ($View in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
    $Base = $null; $Key = $null
    try {
      $Base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
      $Key = $Base.OpenSubKey('SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', $false)
      if ($null -ne $Key) {
        $Value = $Key.GetValue('pv', $null)
        if ($Value -is [string] -and $Value -match '^\\d+\\.\\d+\\.\\d+\\.\\d+$') {
          $Parsed = $null
          if ([Version]::TryParse($Value, [ref]$Parsed) -and $Parsed -gt [Version]'0.0.0.0') { $Found = $true }
        }
      }
    } catch { } finally {
      if ($null -ne $Key) { $Key.Dispose() }
      if ($null -ne $Base) { $Base.Dispose() }
    }
  }
}
if ($Found) { Report PASS WEBVIEW2_REGISTRY_CANDIDATE 'Evergreen registry candidate found; compatible loading is not verified.' }
else { Report UNKNOWN WEBVIEW2_REGISTRY_CANDIDATE 'No Evergreen registry candidate established; fixed-version deployments and access restrictions are not ruled out.' }
Report UNKNOWN WEBVIEW2_LOADER 'The application must link or deploy its official WebView2 loader correctly; registry discovery does not verify it.'
Report UNKNOWN VC_RUNTIME 'Verify the appropriate Microsoft Visual C++ runtime and other application DLL dependencies.'
Report UNKNOWN APP_STARTUP 'Actual WebView creation, policies and graphical startup remain unverified.'
Report INFO NEXT_STEP 'Use official Microsoft WebView2 Runtime and Visual C++ redistribution instructions; this tool installs nothing.'
Report INFO RESULT 'Exit zero means no known blocker, not successful application or browser startup.'
exit 0
`;
}
module.exports = { generatePreflight };
