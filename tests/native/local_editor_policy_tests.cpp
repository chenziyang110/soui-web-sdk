#include <soui/host/webview_host.h>
#include <soui/host/soui_miniblink_host.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace soui::sdk;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

struct fixture {
  std::filesystem::path root;
  fixture() {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned i = 0; i < 32; ++i) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("soui-local-editor-" + std::to_string(seed) + "-" + std::to_string(i));
      if (std::filesystem::create_directory(candidate)) { root = std::move(candidate); break; }
    }
    require(!root.empty(), "cannot create exclusive fixture directory");
    std::ofstream(root / "index.html") << "<!doctype html><iframe sandbox='allow-scripts' srcdoc='<p>editor</p>'></iframe>";
    std::ofstream(root / "editor.js") << "const sandbox=document.createElement('iframe');";
  }
  ~fixture() {
    // This path is assigned only after exclusive fixture creation above.
    std::error_code ignored;
    if (!root.empty()) std::filesystem::remove_all(root, ignored);
  }
};

struct renderer final : webview_host_adapter {
  bool editor_supported;
  unsigned& mounted;
  renderer(bool supported, unsigned& calls) : editor_supported(supported), mounted(calls) {}
  bool supports_local_resource_policy(local_resource_policy policy) const noexcept override {
    return policy == local_resource_policy::strict ||
        (editor_supported && policy == local_resource_policy::isolated_local_editor);
  }
  host_result mount(const host_config&) override { ++mounted; return {true, {}, {}}; }
  host_result unmount() override { return {true, {}, {}}; }
};

host_result attempt(host_config config, bool supported, unsigned& mounted) {
  webview_host host(std::make_unique<renderer>(supported, mounted));
  const auto result = host.start(config);
  require(host.destroy().ok, "fixture renderer failed to close");
  return result;
}

void expect_error(const host_result& result, const char* code, const char* message) {
  require(!result.ok && result.code == code, message);
}
}

int main() {
  try {
    fixture files;
    host_config config;
    config.architecture = sizeof(void*) == 8 ? "x64" : "x86";
    config.resource_root = files.root;
    unsigned mounted = 0;
    require(config.resource_policy == local_resource_policy::strict, "native policy must default to strict");
    expect_error(attempt(config, true, mounted), "E_UNTRUSTED_FRAME", "capability alone must not bypass strict policy");
    require(mounted == 0, "strict rejection must happen before renderer mount");
    config.resource_policy = local_resource_policy::isolated_local_editor;
    {
      dynamic_miniblink_adapter mini;
      require(!mini.supports_local_resource_policy(config.resource_policy), "Miniblink must not advertise the local editor policy");
      expect_error(mini.mount(config), "E_RESOURCE_POLICY_UNSUPPORTED", "direct Miniblink mount must reject editor policy before window setup");
    }
    expect_error(attempt(config, false, mounted), "E_RESOURCE_POLICY_UNSUPPORTED", "unsupported adapter must reject native opt-in");
    require(mounted == 0, "unsupported policy must not reach mount");
    require(attempt(config, true, mounted).ok && mounted == 1, "explicit supported native mode must admit reviewed editor files");
    std::ofstream(files.root / "index.html") << "<!doctype html><p>packaged editor</p>";
    std::ofstream(files.root / "editor.js") << "script.src='https://offline-unused.example/tutorial.js';";
    config.resource_policy = local_resource_policy::strict;
    expect_error(attempt(config, true, mounted), "E_REMOTE_RESOURCE", "strict policy must continue rejecting bundled remote script expressions");
    config.resource_policy = local_resource_policy::isolated_local_editor;
    require(attempt(config, true, mounted).ok && mounted == 2, "native editor policy must accept reviewed fallback code for runtime filtering");
    const auto original = config.resource_root;
    config.resource_root /= "missing";
    expect_error(attempt(config, true, mounted), "E_RESOURCE_MISSING", "editor mode must still validate the resource directory");
    config.resource_root = original;
    std::filesystem::remove(files.root / "index.html");
    expect_error(attempt(config, true, mounted), "E_RESOURCE_MISSING", "editor mode must still require its entry document");
    config.resource_policy = static_cast<local_resource_policy>(999);
    expect_error(attempt(config, true, mounted), "E_RESOURCE_POLICY_UNSUPPORTED", "unknown policy value must be rejected");
    require(mounted == 2, "invalid roots and policy must not reach mount");
    std::cout << "local editor resource policy contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
