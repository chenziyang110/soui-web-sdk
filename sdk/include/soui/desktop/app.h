#pragma once

#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  bool popup_menu{false};
  bool tray{false};
};

struct menu_item {
  std::string id;
  std::string label;
  bool enabled{true};
  bool checked{false};
  bool separator{false};
};

struct tray_options {
  // Platforms use their native application/status icon. The short label is
  // used where the platform supports text status items.
  std::string label{"S"};
  std::string tooltip{"SOUI application"};
  std::vector<menu_item> menu;
};

using window_id = std::uint64_t;
enum class window_event { ready, closed };

struct options {
  std::filesystem::path resource_root;
  std::filesystem::path entry{"index.html"};
  std::filesystem::path user_data_directory;
  std::string title{"SOUI application"};
  int width{1024};
  int height{720};
  bool visible{true};
  // Opt-in bounded reads of app-owned immutable resource_root assets through
  // souiDesktop.readAsset. Does not enable network access or native ES modules.
  bool enable_asset_read{false};
};

struct callbacks {
  // Runs on the UI thread. Return one JSON value; throw to reject the JS Promise.
  // Applications must enforce their own method allowlist and Runtime permissions.
  std::function<std::string(std::string_view method, std::string_view json_parameters)> invoke;
  std::function<void()> ready;
  std::function<void()> closed;
  std::function<void(const result&)> error;
  // Native menu/tray actions are delivered only to this window's callback.
  std::function<void(std::string_view command)> command;
  std::function<void(window_id, window_event)> lifecycle;
};

class application;
struct window_result {
  result status;
  std::shared_ptr<application> window;
};

// One application group per process. The first application owns its event loop;
// create_window adds independently authorized windows in that group. Keep the
// returned window handle alive. Create/call/destroy on the main UI thread.
// Do not destroy an application from its callbacks; close() is safe there.
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
  [[nodiscard]] result quit();
  [[nodiscard]] window_result create_window(options settings, callbacks handlers = {});
  [[nodiscard]] window_id id() const noexcept;
  [[nodiscard]] std::vector<window_id> window_ids() const;
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
  [[nodiscard]] result popup_menu(const std::vector<menu_item>& items);
  [[nodiscard]] result set_tray(const tray_options& settings);
  [[nodiscard]] result clear_tray();
  [[nodiscard]] capabilities get_capabilities() const;
 private:
  struct implementation;
  explicit application(std::unique_ptr<implementation>);
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::desktop
