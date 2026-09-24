#pragma once

#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/core/result/result.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace soui::runtime {

struct task_invocation {
  std::string session_id;
  std::string origin;
  std::string operation_id;
  std::string parameters_json;
};

struct task_outcome {
  result_kind kind{result_kind::success};
  std::string result_json{"{}"};
  std::string message;
};

// This reference is valid only while a registered handler is executing.
class task_control final {
 public:
  task_control(const task_control&) = delete;
  task_control& operator=(const task_control&) = delete;

  [[nodiscard]] bool cancellation_requested() const;
  [[nodiscard]] bool report_progress(std::uint32_t percent, std::string message = {}) const;

 private:
  friend class task_service;
  task_control(std::function<bool()> cancelled,
               std::function<bool(std::uint32_t, std::string)> progress);
  std::function<bool()> cancelled_;
  std::function<bool(std::uint32_t, std::string)> progress_;
};

// Optional public Bridge task module. Applications register bounded native
// handlers before bootstrap; the service owns a fixed worker pool and a bounded
// queue. Handlers must cooperate with cancellation and must not retain control.
class task_service final {
 public:
  static constexpr std::size_t max_workers{2};
  static constexpr std::size_t max_inflight{32};
  static constexpr std::size_t max_subscriptions{128};
  using handler = std::function<task_outcome(const task_invocation&, const task_control&)>;
  using event_sink = std::function<void(std::string_view)>;

  task_service();
  ~task_service();
  task_service(const task_service&) = delete;
  task_service& operator=(const task_service&) = delete;

  [[nodiscard]] bool register_handler(std::string kind, handler callback);
  void set_event_sink(event_sink sink);
  void shutdown_session(const std::string& session_id);
  [[nodiscard]] std::string start(const bridge_request& request);
  [[nodiscard]] std::string query(const bridge_request& request) const;
  [[nodiscard]] std::string cancel(const bridge_request& request);
  [[nodiscard]] std::string subscribe(const bridge_request& request);
  [[nodiscard]] std::string unsubscribe(const bridge_request& request);

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
};

}  // namespace soui::runtime
