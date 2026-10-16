#include "../app/reference-agent.h"
#include "../app/reference-business.h"
#include "../app/reference-gui.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
int run(const std::string& name,const char* fixture_path) {
  auto business=std::make_shared<reference::business>(reference::read_fixture(fixture_path));
  soui::sdk::json_bridge bridge{"app://reference-gui-composition"};
  require(reference::register_business(bridge,business),"Actual business registration failed");
  if(name=="before-factory") {
    reference::agent disabled{bridge,reference::agent_options{},true};
    require(!disabled.enabled()&&!disabled.started(),"GUI registration enabled the endpoint");
    auto* endpoint=bridge.create_local_endpoint();
    require(endpoint&&endpoint->state()==soui::runtime::agent::local_endpoint_state::stopped,"Actual factory was not stopped");
    require(!reference::register_gui_business(bridge),"Frozen registration changed");
    bridge.close_gui_commands();
    require(bridge.poll_gui_commands()==soui::runtime::gui_owner_result::closed,"Actual empty GUI owner did not close");
    require(disabled.stop_owner()==soui::runtime::agent::local_endpoint_stop::stopped,"Disabled endpoint was not stopped");
  } else if(name=="after-factory") {
    require(reference::register_local_business(bridge),"Actual local business registration failed");
    require(bridge.create_local_endpoint()!=nullptr,"Actual factory creation failed");
    require(!reference::register_gui_business(bridge),"GUI registration passed after factory freeze");
  } else return 2;
  // The actual Bridge destructor joins its only service before business release.
  // No window/context fake, GUI dispatch or execution qualification is claimed.
  return 0;
}
} // namespace
int main(int count,char** arguments) {
  try {if(count!=3)return 2;return run(arguments[1],arguments[2]);}
  catch(...) {std::cerr<<"Reference GUI registration failed.\n";return 1;}
}
