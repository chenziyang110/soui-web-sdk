#pragma once

#include <string>

#include <soui/runtime/core/result/result.h>
#include <soui/runtime/services/permissions/permission_store.h>

namespace soui::runtime {

struct desktop_request_context {
  std::string session_id;
  std::string origin;
  std::string token;
  bool user_gesture{false};
};

struct desktop_response {
  result_kind kind{result_kind::failure};
  std::string code;
  std::string message;
  std::string value;

  [[nodiscard]] bool ok() const noexcept { return kind == result_kind::success; }
};

class directory_picker {
 public:
  explicit directory_picker(const permission_store& permissions) : permissions_(permissions) {}

  [[nodiscard]] desktop_response open(void* owner, const desktop_request_context& context,
                                      const std::string& initial_directory = {}) const;

 private:
  const permission_store& permissions_;
};

class window_service {
 public:
  explicit window_service(const permission_store& permissions) : permissions_(permissions) {}

  [[nodiscard]] desktop_response set_title(void* window, const desktop_request_context& context,
                                            const std::string& title) const;
  [[nodiscard]] desktop_response begin_drag(void* window, const desktop_request_context& context) const;
  [[nodiscard]] desktop_response minimize(void* window, const desktop_request_context& context) const;
  [[nodiscard]] desktop_response toggle_maximize(void* window, const desktop_request_context& context) const;
  [[nodiscard]] desktop_response close(void* window, const desktop_request_context& context) const;
  [[nodiscard]] desktop_response set_foreground(void* window,
                                                const desktop_request_context& context) const;

 private:
  const permission_store& permissions_;
};

class clipboard_service {
 public:
  explicit clipboard_service(const permission_store& permissions) : permissions_(permissions) {}

  [[nodiscard]] desktop_response write_text(const desktop_request_context& context,
                                            const std::string& text) const;

 private:
  const permission_store& permissions_;
};

}  // namespace soui::runtime
