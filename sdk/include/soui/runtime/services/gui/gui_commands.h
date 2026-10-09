#pragma once
#include <soui/runtime/services/task/task_service.h>
#include <functional>
#include <string>
#include <vector>

namespace soui::runtime {
enum class gui_operation { perform, snapshot, wait_for };
struct gui_command_definition {
  std::string family_name, resource, id;
  gui_operation operation{gui_operation::perform};
  std::string action;
  std::string start_capability, query_capability, cancel_capability;
  std::string input_schema_json, result_schema_json;
  // Empty or one existing, explicitly local-enabled typed action/family method.
  // Snapshot/wait_for rows must be empty and therefore cannot consume effects.
  std::string effect_method;
  // Required native whitelist projection, outside locks; never echo a snapshot
  // by default. The SDK adds its own authoritative effect receipt separately.
  // Worker-thread whitelist. It must not access Host/window objects and its
  // captured business storage must outlive Bridge's actual task_service join.
  std::function<std::string(const task_outcome&)> project_result;
};
enum class gui_owner_result { idle, dispatched, busy, wrong_owner, pending, closed };
} // namespace soui::runtime
