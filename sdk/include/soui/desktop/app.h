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

struct open_file_options {
  std::string title;
  // ASCII alphanumeric suffixes without dots; empty means all files.
  std::vector<std::string> extensions;
  bool multiple{false};
};

struct save_file_options {
  std::string title;
  std::vector<std::string> extensions;
  // Optional portable basename, never a directory or initial path.
  std::string default_name;
};

struct file_selection_result {
  result status;
  std::vector<std::string> paths;
};

struct path_result {
  result status;
  std::filesystem::path value;
};

// Resolve the running executable's sibling web directory, or Contents/Resources/web
// inside a macOS app bundle. Does not use argv[0], cwd or an environment fallback.
// Synchronous startup metadata I/O; requires a trusted, stable app deployment.
// Returns a canonical existing directory, not a filesystem sandbox guarantee.
[[nodiscard]] path_result application_resource_directory();

struct window_state {
  bool minimized{false};
  bool maximized{false};
};

struct window_state_result {
  result status;
  window_state value;
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
  bool window_minimize{false};
  bool window_maximize{false};
  bool window_restore{false};
  bool window_state_observation{false};
  // Implementation availability, not a promise that any combination is free.
  bool global_shortcuts{false};
  bool file_open_dialog{false};
  bool file_save_dialog{false};
};

enum class shortcut_key {
  f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11,
  left, right, up, down, home, end, page_up, page_down, insert_key, delete_key
};
namespace shortcut_modifiers {
inline constexpr std::uint32_t control = 1;
inline constexpr std::uint32_t alt = 2;
inline constexpr std::uint32_t shift = 4;
// Command on macOS, Control on other platforms.
inline constexpr std::uint32_t primary = 8;
}
struct global_shortcut {
  shortcut_key key{shortcut_key::f1};
  std::uint32_t modifiers{0};
};

struct menu_item {
  std::string id;
  std::string label;
  bool enabled{true};
  bool checked{false};
  bool separator{false};
  // Nonempty children form a submenu: empty ID, nonempty label, not checked.
  // Appended to preserve existing flat aggregate initializers.
  std::vector<menu_item> children{};
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

// Trusted-application preauthorization, not proof of a user gesture.
// These permissions do not accept OS prompts or grant filesystem access.
struct service_permissions {
  bool window_properties{false};
  bool clipboard_read{false};
  bool clipboard_write{false};
  bool directory_dialog{false};
  bool file_open_dialog{false};
  bool file_save_dialog{false};
};

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
  // Opt-in per-window loopback HTTP origin for standard frontend resource URLs.
  // Keeps file:// loading as the default; not a general-purpose network server.
  bool enable_resource_server{false};
  // Explicit permission for current-page desktop.window.* controls and state events.
  bool enable_window_controls{false};
  service_permissions services;
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
  // Observed native state, not a promise that an earlier request was applied.
  std::function<void(window_id, window_state)> state_changed;
  // User-requested close approval. Default allows; false/exception cancels.
  // Never wait for a webpage Promise here. close()/quit() bypass this hook.
  std::function<bool(window_id)> close_requested;
  // Native-only notification, not proof of a user gesture or Web permission.
  std::function<void(window_id, std::string_view id)> global_shortcut;
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
  // Request approval; may defer until an active callback unwinds.
  // Success means accepted for processing, not proof the window has closed.
  [[nodiscard]] result request_close();
  [[nodiscard]] result request_quit();
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
  // Success accepts a native request; query/events report observed state.
  [[nodiscard]] result minimize();
  [[nodiscard]] result maximize();
  [[nodiscard]] result restore();
  [[nodiscard]] window_state_result get_window_state() const;
  [[nodiscard]] value_result clipboard_read();
  [[nodiscard]] result clipboard_write(std::string_view text);
  [[nodiscard]] value_result choose_directory();
  // Path selection only: no file reads/writes or persistent access grants.
  // Open selects 1..32 paths (one unless multiple); save selects one path.
  // Cancellation is E_CANCELLED; owner closure remains a closed error.
  [[nodiscard]] file_selection_result choose_files(const open_file_options& = {});
  [[nodiscard]] value_result choose_save_file(const save_file_options& = {});
  // Popup/tray trees allow 0..64 total nodes and at most 4 levels (root = 1).
  // Leaf IDs are unique per tree; disabled ancestors suppress leaf commands.
  [[nodiscard]] result popup_menu(const std::vector<menu_item>& items);
  [[nodiscard]] result set_tray(const tray_options& settings);
  [[nodiscard]] result clear_tray();
  // At most 32 registrations per window, including pending native cleanup.
  // IDs/combinations are unique; at least Control, Alt or Primary is required.
  // Registrations survive navigation; close revokes delivery before cleanup.
  // shortcut_cleanup_pending reserves an inactive ID until unregister succeeds.
  [[nodiscard]] result register_global_shortcut(std::string_view id, const global_shortcut&);
  // Unknown IDs succeed. Failure keeps an inactive record for cleanup retry.
  [[nodiscard]] result unregister_global_shortcut(std::string_view id);
  [[nodiscard]] capabilities get_capabilities() const;
 private:
  struct implementation;
  explicit application(std::unique_ptr<implementation>);
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::desktop
