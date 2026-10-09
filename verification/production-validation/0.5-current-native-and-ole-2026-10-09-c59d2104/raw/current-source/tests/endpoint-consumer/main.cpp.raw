#include <soui/json_bridge.h>
#include <soui/runtime/agent/windows_local_endpoint.h>
#include <iostream>

int main() {
  using namespace soui::runtime::agent;
  soui::sdk::json_bridge bridge{"app://installed-endpoint-consumer"};
  if (!bridge.register_method("consumer.echo", "consumer.echo", [](std::string_view) { return "{}"; })) return 1;
  auto* endpoint = bridge.create_local_endpoint();
  if (!endpoint || endpoint->state() != local_endpoint_state::stopped ||
      bridge.set_session_capabilities({"consumer.echo"})) return 2;
  if (endpoint->start({"not/a/local/component", 5000, 10000}) != local_endpoint_error::invalid_options) return 3;
  endpoint->request_stop();
  if (endpoint->poll_stop() != local_endpoint_stop::stopped) return 4;
  std::cout << "installed public json_bridge endpoint factory stopped\n";
  // Borrowed endpoint expires at bridge destruction; it is never deleted here.
  return 0;
}
