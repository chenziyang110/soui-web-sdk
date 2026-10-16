#include <soui/host/webview_host.h>
#include <iostream>
#include <memory>
class probe_adapter final : public soui::sdk::webview_host_adapter {
 public:
  soui::sdk::host_result mount(const soui::sdk::host_config&) override { return {true, {}, "reached-adapter"}; }
  soui::sdk::host_result unmount() override { return {true, {}, {}}; }
};
int wmain(int argc,wchar_t** argv) {
  if(argc!=2)return 2;
  soui::sdk::soui_miniblink_host host(std::make_unique<probe_adapter>());
  soui::sdk::host_config config;config.architecture="x64";config.resource_root=argv[1];
  const auto result=host.start(config);
  std::cout<<(result.ok?"accepted":result.code)<<'\n';
  if(result.ok&&!host.destroy().ok)return 3;
  return result.ok?0:1;
}
