#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace soui::desktop {

struct result {
  bool ok{false};
  std::string code;
  std::string message;
  static result success() { return {true, {}, {}}; }
  static result failure(std::string code, std::string message) {
    return {false, std::move(code), std::move(message)};
  }
};

struct value_result {
  result status;
  std::string value;
};

struct capabilities {
  std::string backend;
  bool clipboard_read{false};
  bool clipboard_write{false};
  bool directory_dialog{false};
  bool window_resize{false};
  bool window_focus{false};
};

struct options {
  std::filesystem::path resource_root;
  std::filesystem::path entry{"index.html"};
  std::filesystem::path user_data_directory;
  std::string title{"SOUI application"};
  int width{1024};
  int height{720};
  bool visible{true};
};

struct callbacks {
  // Runs on the UI thread. Return one JSON value; throw to reject the JS Promise.
  // Applications must enforce their own method allowlist and Runtime permissions.
  std::function<std::string(std::string_view method, std::string_view json_parameters)> invoke;
  std::function<void()> ready;
  std::function<void()> closed;
  std::function<void(const result&)> error;
};

// One application/main window per process. Create, call and destroy on the main
// UI thread. Do not destroy the application from one of its callbacks.
// No engine-specific types leak into this public interface.
class application final {
 public:
  explicit application(options settings, callbacks handlers = {});
  ~application();
  application(const application&) = delete;
  application& operator=(const application&) = delete;
  [[nodiscard]] result initialize();
  [[nodiscard]] result run();
  [[nodiscard]] result close();
  [[nodiscard]] result navigate(const std::filesystem::path& relative_html);
  [[nodiscard]] result reload();
  [[nodiscard]] result emit(std::string_view event, std::string_view json_value);
  // The only cross-thread entry. The application must outlive all callers.
  // Queued work is discarded after close and always runs on the UI thread.
  [[nodiscard]] result dispatch(std::function<void()> work);
  [[nodiscard]] result set_title(std::string_view title);
  [[nodiscard]] result resize(int width, int height);
  [[nodiscard]] result show(bool visible);
  [[nodiscard]] result focus();
  [[nodiscard]] value_result clipboard_read();
  [[nodiscard]] result clipboard_write(std::string_view text);
  [[nodiscard]] value_result choose_directory();
  [[nodiscard]] capabilities get_capabilities() const;
 private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::desktop
