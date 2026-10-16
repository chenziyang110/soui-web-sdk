#pragma once
#include <soui/json_bridge.h>
#include <string>
#include <vector>

namespace reference {
// App-only composition. Local business must be registered first; endpoint
// creation and Host bootstrap must happen after this succeeds.
[[nodiscard]] bool register_gui_business(soui::sdk::json_bridge& bridge);
[[nodiscard]] std::vector<std::string> gui_command_methods();
} // namespace reference
