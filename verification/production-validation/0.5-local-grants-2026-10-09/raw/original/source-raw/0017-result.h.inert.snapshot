#pragma once

#include <string>

namespace soui::runtime {

enum class result_kind {
  success,
  failure,
  user_cancelled,
  task_cancelled,
  timeout_observed,
  partial,
  input_required,
};

struct operation_result {
  result_kind kind{result_kind::success};
  std::string task_id;
  std::string message;

  operation_result() = default;
  operation_result(result_kind result_kind, std::string result_task_id, std::string result_message = {});

  [[nodiscard]] bool terminal() const noexcept {
    return kind != result_kind::timeout_observed && kind != result_kind::input_required;
  }
};

}  // namespace soui::runtime
