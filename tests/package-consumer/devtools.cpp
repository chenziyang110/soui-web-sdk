#include <soui/devtools.h>
#include <soui/host/webview_host.h>
#include <soui/host/soui_miniblink_host.h>
#include <memory>
#include <type_traits>

// This adapter intentionally implements only the pre-DevTools required API.
class original_adapter final : public soui::sdk::webview_host_adapter {
 public:
  soui::sdk::host_result mount(const soui::sdk::host_config&) override {return {true,{},{}};}
  soui::sdk::host_result unmount() override {return {true,{},{}};}
};
static_assert(!std::is_copy_constructible_v<soui::sdk::webview_host>);
static_assert(std::is_default_constructible_v<soui::sdk::dynamic_miniblink_adapter>);
static_assert(std::is_constructible_v<soui::sdk::dynamic_miniblink_adapter, soui::sdk::diagnostics::mode>);
static_assert(std::is_constructible_v<soui::sdk::dynamic_miniblink_adapter,
    soui::sdk::diagnostics::mode, soui::sdk::miniblink_local_resource_options>);
int main() {
  // Resolves the opt-in constructor from the installed static library without
  // initializing a renderer or granting a diagnostic session.
  auto local_adapter=std::make_unique<soui::sdk::dynamic_miniblink_adapter>(
      soui::sdk::diagnostics::mode::disabled, soui::sdk::miniblink_local_resource_options{});
  namespace dt=soui::sdk::devtools;
  soui::sdk::webview_host host(std::make_unique<original_adapter>());
  if(host.devtools_capabilities().native_open)return 1;
  if(!host.configure_devtools({}).ok)return 2;
  dt::configuration config;config.access=dt::policy::application_controlled;
  config.ending=dt::termination::destroy_target;
  const auto configured=host.configure_devtools(config);
  if(configured.ok||configured.code!="E_NOT_SUPPORTED")return 3;
  dt::grant absent;
  if(host.open_devtools(absent).accepted())return 4;
  if(host.observe_devtools().grant_valid)return 5;
  if(host.devtools_operation(999).status!=dt::operation_status::failed)return 6;
  return host.destroy().ok?0:7;
}
