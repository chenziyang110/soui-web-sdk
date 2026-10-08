#include <soui/host/runtime_profile.h>
#include <iostream>
#include <stdexcept>

using namespace soui::sdk;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main() {
  try {
    runtime_environment env{true, 10, 0, 19045, true, false, false, "x64", "x64"};
    const renderer_availability both{true, true};
    require(select_runtime_profile(env, both, runtime_profile::automatic).backend == renderer_backend::webview2, "Modern default");
    require(select_runtime_profile(env, both, runtime_profile::compatibility).backend == renderer_backend::miniblink, "Explicit compatibility");
    require(!select_runtime_profile(env, {true, false}, runtime_profile::modern).ok, "Missing modern runtime must not downgrade");
    require(!select_runtime_profile(env, both, runtime_profile::compatibility, true).ok, "Modern app conflicts with compatibility");
    env.os_major = 6; env.os_minor = 1;
    require(select_runtime_profile(env, both, runtime_profile::automatic).backend == renderer_backend::miniblink, "Win7 selects compatibility");
    require(!select_runtime_profile(env, both, runtime_profile::automatic, true).ok, "Modern app must reject Win7");
    env.os_minor = 0;
    require(!select_runtime_profile(env, both, runtime_profile::automatic).ok, "Vista rejected");
    env.os_major = 10; env.os_minor = 0; env.windows_pe = true;
    require(select_runtime_profile(env, both, runtime_profile::automatic).profile == runtime_profile::pe, "PE detected despite modern OS number");
    require(!select_runtime_profile(env, both, runtime_profile::modern).ok, "No implicit modern PE support");
    env.process_architecture = "x86";
    require(!select_runtime_profile(env, both, runtime_profile::pe).ok, "No WoW64 assumption on PE");
    env.windows_pe = false;
    require(select_runtime_profile(env, both, runtime_profile::compatibility).ok, "x86 on normal x64 Windows");
    require(!select_runtime_profile(env, both, runtime_profile::pe).ok, "PE cannot be forced on desktop");
    env.pe_known = false;
    require(!select_runtime_profile(env, both, runtime_profile::automatic).ok, "Unknown PE state fails closed");
    env.pe_known = true; env.os_version_known = false;
    require(!select_runtime_profile(env, both, runtime_profile::automatic).ok, "Unknown OS fails closed");
    env.os_version_known = true; env.native_architecture = "arm64";
    require(!select_runtime_profile(env, both, runtime_profile::automatic).ok, "ARM is not implicitly certified");
    const auto current = inspect_runtime_environment();
    require(current.os_version_known && current.pe_known, "Current OS probe");
    std::cout << "runtime profiles passed; observed " << current.os_major << '.' << current.os_minor
              << '.' << current.os_build << ' ' << current.native_architecture << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
