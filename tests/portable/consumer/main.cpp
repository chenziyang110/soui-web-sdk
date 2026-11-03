#include <soui/runtime/core/events/event_bus.h>
#include <soui/runtime/core/result/result.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/host/session/webview_session.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <iostream>
#include <stdexcept>

int main() {
  try {
    soui::runtime::permission_store permissions;
    const auto token = permissions.issue("consumer", "app://consumer", {"app.read"});
    if (!permissions.authorize("consumer", "app://consumer", token, "app.read"))
      throw std::runtime_error("installed permissions failed");
    permissions.revoke_session("consumer");
    if (permissions.authorize("consumer", "app://consumer", token, "app.read"))
      throw std::runtime_error("installed revocation failed");
    soui::runtime::task_store tasks;
    soui::runtime::webview_session session;
    if (session.terminal() || !session.close() || !session.terminal())
      throw std::runtime_error("installed lifecycle failed");
    std::cout << "Installed Runtime consumer passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
