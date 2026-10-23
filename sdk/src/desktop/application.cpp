#include <soui/desktop/app.h>
#include "backend.h"
#include "protocol.h"
#include "assets.h"
#include "asset_executor.h"
#include "resource_server.h"
#include "resource_uri.h"
#include "../runtime/platform/secure_random.h"
#include "../runtime/platform/strict_utf8.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <limits>
#include <set>
#include <thread>

namespace soui::desktop {
namespace {
std::atomic<bool> process_application{false};
std::atomic<window_id> next_window_id{1};
// Process-wide identity prevents stale notifications from matching a later
// registration, including one created by a different application group.
std::atomic<std::uint64_t> next_shortcut_token{1};
bool valid_text(std::string_view value, std::size_t limit) {
  return value.size() <= limit && value.find('\0') == std::string_view::npos &&
    soui::runtime::detail::valid_utf8(value);
}
result unavailable() { return result::failure("closed", "The desktop application is not active."); }
result invalid_file_options() {
  return result::failure("invalid_params", "File dialog parameters have invalid fields, types or bounds.");
}
result normalize_file_options(std::string_view title, const std::vector<std::string>& extensions,
                              std::vector<std::string>& normalized) {
  if (!valid_text(title, 4096) || extensions.size() > 16) return invalid_file_options();
  normalized.clear();
  for (const auto& source : extensions) {
    if (source.empty() || source.size() > 16) return invalid_file_options();
    auto extension = source;
    for (auto& character : extension) {
      if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
      else if (!(character >= 'a' && character <= 'z') && !(character >= '0' && character <= '9'))
        return invalid_file_options();
    }
    if (std::find(normalized.begin(), normalized.end(), extension) == normalized.end())
      normalized.push_back(std::move(extension));
  }
  return result::success();
}
bool portable_basename(std::string_view name) {
  if (!valid_text(name, 255)) return false;
  if (name.empty()) return true;
  if (name.back() == '.' || name.back() == ' ') return false;
  for (std::size_t i = 0; i < name.size(); ++i) {
    const auto byte = static_cast<unsigned char>(name[i]);
    if (byte < 32 || byte == 127 || std::string_view("/\\:*?\"<>|").find(name[i]) != std::string_view::npos)
      return false;
    // UTF-8 C1 controls are also not portable display names.
    if (byte == 0xc2 && i + 1 < name.size() &&
        static_cast<unsigned char>(name[i + 1]) >= 0x80 &&
        static_cast<unsigned char>(name[i + 1]) <= 0x9f) return false;
  }
  std::string stem(name.substr(0, name.find('.')));
  while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
  for (auto& character : stem)
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
  if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
      stem == "CLOCK$" || stem == "CONIN$" || stem == "CONOUT$") return false;
  if (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) {
    const auto suffix = stem.substr(3);
    if ((suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') ||
        suffix == "\xc2\xb9" || suffix == "\xc2\xb2" || suffix == "\xc2\xb3") return false;
  }
  return true;
}
bool local_absolute_path(std::string_view value) {
  if (value.empty() || !valid_text(value, 32768)) return false;
  try { return std::filesystem::u8path(value.begin(), value.end()).is_absolute(); }
  catch (...) { return false; }
}
result validate_file_paths(const std::vector<std::string>& paths, bool multiple) {
  const auto invalid = result::failure("invalid_result", "Native file dialog returned invalid, oversized or nonlocal paths.");
  if (paths.empty() || paths.size() > (multiple ? 32U : 1U)) return invalid;
  std::size_t total = 0;
  for (const auto& path : paths) {
    if (!local_absolute_path(path) || path.size() > 131072 - total) return invalid;
    total += path.size();
  }
  return result::success();
}
bool parse_file_options(const nlohmann::json& params, bool save,
                        std::string& title, std::vector<std::string>& extensions,
                        bool& multiple, std::string& default_name) {
  if (!params.is_object()) return false;
  for (auto field = params.begin(); field != params.end(); ++field) {
    if (field.key() == "title") {
      if (!field.value().is_string()) return false;
      title = field.value().get<std::string>();
    } else if (field.key() == "extensions") {
      if (!field.value().is_array() || field.value().size() > 16) return false;
      for (const auto& extension : field.value()) {
        if (!extension.is_string()) return false;
        extensions.push_back(extension.get<std::string>());
      }
    } else if (!save && field.key() == "multiple") {
      if (!field.value().is_boolean()) return false;
      multiple = field.value().get<bool>();
    } else if (save && field.key() == "defaultName") {
      if (!field.value().is_string()) return false;
      default_name = field.value().get<std::string>();
    } else return false;
  }
  return true;
}
result normalize_shortcut(const global_shortcut& input, global_shortcut& output) {
  using namespace shortcut_modifiers;
  if (input.key < shortcut_key::f1 || input.key > shortcut_key::delete_key ||
      (input.modifiers & ~(control | alt | shift | primary)) ||
      !(input.modifiers & (control | alt | primary)))
    return result::failure("invalid_shortcut", "A supported key and Control, Alt or Primary modifier are required.");
  output = input;
#if !defined(__APPLE__)
  if (output.modifiers & primary) output.modifiers = (output.modifiers & ~primary) | control;
#endif
  return result::success();
}
result validate_menu_level(const std::vector<menu_item>& items, unsigned depth,
                           std::size_t& count, std::set<std::string>& ids) {
  if (depth > 4 || items.size() > 64 - count)
    return result::failure("invalid_menu", "Menus allow at most 64 total nodes and 4 levels.");
  for (const auto& item : items) {
    if (++count > 64)
      return result::failure("invalid_menu", "Menus allow at most 64 total nodes and 4 levels.");
    if (item.separator) {
      if (!item.id.empty() || !item.label.empty() || !item.children.empty())
        return result::failure("invalid_menu", "Separators must have empty ID, label and children.");
    } else {
      if (item.label.empty() || !valid_text(item.label, 4096))
        return result::failure("invalid_menu", "Menu labels must be nonempty bounded UTF-8.");
      if (!item.children.empty()) {
        if (!item.id.empty() || item.checked)
          return result::failure("invalid_menu", "Submenus must have empty IDs and cannot be checked.");
        const auto valid = validate_menu_level(item.children, depth + 1, count, ids);
        if (!valid.ok) return valid;
      } else if (!detail::valid_name(item.id) || !ids.insert(item.id).second) {
        return result::failure("invalid_menu", "Leaf menu IDs must be valid and unique across the tree.");
      }
    }
  }
  return result::success();
}
result validate_menu(const std::vector<menu_item>& items) {
  std::size_t count = 0;
  std::set<std::string> ids;
  return validate_menu_level(items, 1, count, ids);
}
void collect_enabled_commands(const std::vector<menu_item>& items, std::set<std::string>& ids) {
  for (const auto& item : items) {
    if (item.separator || !item.enabled) continue;
    if (item.children.empty()) ids.insert(item.id);
    else collect_enabled_commands(item.children, ids);
  }
}
std::set<std::string> enabled_commands(const std::vector<menu_item>& items) {
  std::set<std::string> ids;
  collect_enabled_commands(items, ids);
  return ids;
}
struct application_state;
// Workers retain only this independent gate, never an application or backend.
// dispatch is non-inline and thread-safe. Revocation precedes backend teardown.
struct asset_completion_channel {
  std::mutex mutex;
  detail::backend* target{nullptr};
  void revoke() {
    std::lock_guard<std::mutex> lock(mutex);
    target = nullptr;
  }
  void post(std::function<void()> work) {
    std::lock_guard<std::mutex> lock(mutex);
    if (target) (void)target->dispatch(std::move(work));
  }
};
struct application_group {
  std::vector<std::weak_ptr<application_state>> windows;
  std::weak_ptr<application_state> root;
  unsigned callback_depth{0};
  bool draining{false};
  bool quitting{false};
  bool stopped{false};
  bool loop_started{false};
  bool seen_window{false};
  bool approving_close{false};
  bool close_error_callback{false};
  bool pending_quit{false};
  std::uint64_t window_revision{0};
  std::size_t open_count() const;
  std::size_t reserved_count() const;
  void drain() noexcept;
  void stop_if_empty() noexcept;
  result quit();
  result request_quit();
};
struct callback_scope {
  unsigned& depth;
  explicit callback_scope(unsigned& value) : depth(value) { ++depth; }
  ~callback_scope() { --depth; }
  callback_scope(const callback_scope&) = delete;
  callback_scope& operator=(const callback_scope&) = delete;
};
bool desktop_service(std::string_view method) {
  return method == "desktop.window.setTitle" || method == "desktop.window.resize" ||
    method == "desktop.window.show" || method == "desktop.window.focus" ||
    method == "desktop.clipboard.readText" || method == "desktop.clipboard.writeText" ||
    method == "desktop.dialog.chooseDirectory" || method == "desktop.dialog.openFiles" ||
    method == "desktop.dialog.saveFile";
}
struct application_state : std::enable_shared_from_this<application_state> {
  options settings;
  callbacks handlers;
  std::unique_ptr<detail::backend> native;
  std::shared_ptr<application_group> group;
  window_id identity{0};
  bool is_root{true};
  const std::thread::id owner{std::this_thread::get_id()};
  std::atomic<bool> accepting{false};
  bool initialized{false};
  bool owns_process_slot{false};
  bool notified_closed{false};
  bool ready{false};
  std::string token;
  detail::document current;
  std::uint64_t generation{0};
  std::uint64_t last_request{0};
  struct queued_request { std::string payload; std::string source; detail::message_frame frame; };
  std::deque<queued_request> requests;
  std::deque<result> errors;
  std::deque<std::function<void()>> work_items;
  struct queued_command { std::string id; std::uint64_t revision; };
  std::deque<queued_command> commands;
  std::uint64_t action_revision{0};
  std::uint64_t popup_revision{0};
  std::uint64_t tray_revision{0};
  std::set<std::string> popup_commands;
  std::set<std::string> tray_commands;
  struct shortcut_registration {
    std::string id;
    global_shortcut chord;
    std::uint64_t token;
    bool active{false};
    bool native_pending{false};
    bool revoked{false};
  };
  std::vector<shortcut_registration> shortcuts;
  // Separate from menu revisions and document generations. Each notification
  // still rechecks its token and active flag immediately before delivery.
  std::deque<std::uint64_t> shortcut_events;
  std::atomic<std::size_t> pending_work{0};
  bool draining{false};
  bool pending_closed{false};
  bool pending_close_request{false};
  bool pending_ready{false};
  std::uint64_t ready_generation{0};
  bool pending_state_changed{false};
  window_state observed_state;
  std::uint64_t state_generation{0};
  bool state_document_ready{false};
  detail::asset_cancellation asset_session;
  bool assets_submitted{false};
  std::shared_ptr<asset_completion_channel> asset_channel = std::make_shared<asset_completion_channel>();
  detail::resource_server resources;

  void cancel_assets() noexcept {
    if (asset_session) {
      asset_session->store(true);
      if (assets_submitted) detail::process_asset_executor().cancel(asset_session);
      asset_session.reset();
    }
    assets_submitted = false;
  }
  result submit_asset(std::uint64_t id, std::string_view path) {
    const auto selected_generation = generation;
    const std::weak_ptr<application_state> weak = shared_from_this();
    const auto channel = asset_channel;
    assets_submitted = true;
    return detail::process_asset_executor().submit(settings.resource_root, path, asset_session,
      [weak, channel, selected_generation, id](detail::asset_result asset, detail::asset_permit permit) mutable {
        channel->post([weak, selected_generation, id, asset = std::move(asset), permit = std::move(permit)]() mutable {
          if (const auto live = weak.lock(); live && live->accepting.load() &&
              live->generation == selected_generation) {
            // Reuse common serialization, including a nested native modal loop.
            live->work_items.push_back(
              [weak, selected_generation, id, asset = std::move(asset), permit = std::move(permit)]() mutable {
                const auto current = weak.lock();
                if (!current || !current->accepting.load() || current->generation != selected_generation) return;
                nlohmann::json response{{"id", id}, {"ok", asset.status.ok}};
                if (asset.status.ok)
                  response["result"] = {{"mime", asset.mime}, {"size", asset.size}, {"base64", asset.base64}};
                else response["error"] = {{"code", asset.status.code}, {"message", asset.status.message}};
                const auto sent = current->deliver(std::move(response));
                if (!sent.ok) current->report(sent);
              });
            live->pending_work.fetch_add(1);
            live->drain();
          }
        });
      });
  }

  result check_thread() const {
    if (std::this_thread::get_id() != owner)
      return result::failure("wrong_thread", "Use application.dispatch from worker threads.");
    return result::success();
  }
  result check() const {
    const auto thread = check_thread();
    if (!thread.ok) return thread;
    return initialized && accepting.load() ? result::success() : unavailable();
  }
  void report(result failure) noexcept {
    if (errors.size() < 8) errors.push_back(std::move(failure));
    drain();
  }
  void drain() noexcept {
    if (group->callback_depth || group->approving_close || draining) return;
    draining = true;
    while (true) {
      if (pending_closed) {
        pending_closed = false;
        requests.clear();
        work_items.clear();
        commands.clear();
        shortcut_events.clear();
        pending_ready = false;
        pending_state_changed = false;
        pending_close_request = false;
        ++group->callback_depth;
        try { if (handlers.closed) handlers.closed(); } catch (...) {}
        try { if (handlers.lifecycle) handlers.lifecycle(identity, window_event::closed); } catch (...) {}
        --group->callback_depth;
      } else if (!errors.empty()) {
        auto error = std::move(errors.front()); errors.pop_front();
        if (handlers.error) {
          ++group->callback_depth;
          group->close_error_callback = error.code == "close_callback_failed";
          try { handlers.error(error); } catch (...) {}
          group->close_error_callback = false;
          --group->callback_depth;
        }
      } else if (group->pending_quit) {
        group->pending_quit = false;
        (void)group->request_quit();
      } else if (pending_close_request) {
        pending_close_request = false;
        if (accepting.load()) (void)request_close();
      } else if (pending_ready) {
        pending_ready = false;
        if (accepting.load() && ready_generation == generation) {
          ++group->callback_depth;
          const auto active_generation = generation;
          try { if (handlers.ready) handlers.ready(); }
          catch (...) { report(result::failure("callback_failed", "The ready callback failed.")); }
          try {
            if (accepting.load() && generation == active_generation && handlers.lifecycle)
              handlers.lifecycle(identity, window_event::ready);
          } catch (...) { report(result::failure("callback_failed", "The lifecycle callback failed.")); }
          --group->callback_depth;
        }
      } else if (pending_state_changed) {
        pending_state_changed = false;
        const auto snapshot = observed_state;
        const auto snapshot_generation = state_generation;
        const bool notify_page = state_document_ready;
        if (accepting.load()) {
          ++group->callback_depth;
          try { if (handlers.state_changed) handlers.state_changed(identity, snapshot); }
          catch (...) { report(result::failure("callback_failed", "The window state callback failed.")); }
          // Native callbacks belong to the window. Page notifications additionally
          // belong to the document that was ready when the observation arrived.
          if (settings.enable_window_controls && notify_page && accepting.load() &&
              ready && generation == snapshot_generation) {
            const auto sent = deliver({{"event", "desktop.window.stateChanged"},
              {"value", {{"minimized", snapshot.minimized}, {"maximized", snapshot.maximized}}}});
            if (!sent.ok) report(sent);
          }
          --group->callback_depth;
        }
      } else if (!commands.empty()) {
        auto command = std::move(commands.front()); commands.pop_front();
        if (accepting.load() && command.revision == action_revision &&
            command_allowed(command.id) && handlers.command) {
          ++group->callback_depth;
          try { handlers.command(command.id); }
          catch (...) { report(result::failure("callback_failed", "A native command callback failed.")); }
          --group->callback_depth;
        }
      } else if (!shortcut_events.empty()) {
        const auto selected = shortcut_events.front(); shortcut_events.pop_front();
        const auto found = std::find_if(shortcuts.begin(), shortcuts.end(), [selected](const auto& item) {
          return item.token == selected && item.active;
        });
        if (accepting.load() && found != shortcuts.end() && handlers.global_shortcut) {
          // The callback may unregister/re-register, invalidating the record.
          const auto id = found->id;
          ++group->callback_depth;
          try {
            const auto callback = handlers.global_shortcut;
            callback(identity, id);
          }
          catch (...) { report(result::failure("callback_failed", "A global shortcut callback failed.")); }
          --group->callback_depth;
        }
      } else if (!requests.empty()) {
        auto request = std::move(requests.front()); requests.pop_front();
        receive(request.payload, request.source, request.frame);
      } else if (!work_items.empty()) {
        auto work = std::move(work_items.front()); work_items.pop_front();
        pending_work.fetch_sub(1);
        if (accepting.load()) {
          ++group->callback_depth;
          try { work(); }
          catch (...) { report(result::failure("callback_failed", "A dispatched callback failed.")); }
          --group->callback_depth;
        }
      } else break;
    }
    draining = false;
    group->drain();
  }
  bool command_allowed(std::string_view command) const {
    const std::string value(command);
    return popup_commands.count(value) || tray_commands.count(value);
  }
  void on_state_changed(window_state value) {
    if (!accepting.load()) return;
    observed_state = value;
    state_generation = generation;
    state_document_ready = ready;
    pending_state_changed = true;
    drain();
  }
  void on_command(std::string_view command) {
    if (!accepting.load() || !detail::valid_name(command) || !command_allowed(command) ||
        commands.size() >= 128) return;
    commands.push_back({std::string(command), action_revision});
    drain();
  }
  void on_global_shortcut(std::uint64_t selected) {
    if (!accepting.load() || shortcut_events.size() >= 128 ||
        std::none_of(shortcuts.begin(), shortcuts.end(), [selected](const auto& item) {
          return item.token == selected && item.active;
        })) return;
    shortcut_events.push_back(selected);
    // Backends already defer OS notifications on a window-lifetime queue. Do
    // not route these through renderer-dependent application dispatch again.
    drain();
  }
  void revoke_shortcuts() noexcept {
    for (auto& item : shortcuts) { item.active = false; item.revoked = true; }
    shortcut_events.clear();
  }
  result cleanup_shortcuts() {
    // Revoke all registrations before the first native cleanup can pump events.
    revoke_shortcuts();
    result status = result::success();
    callback_scope scope(group->callback_depth);
    std::vector<std::uint64_t> pending;
    for (const auto& item : shortcuts) pending.push_back(item.token);
    for (const auto selected : pending) {
      auto item = std::find_if(shortcuts.begin(), shortcuts.end(), [selected](const auto& value) {
        return value.token == selected;
      });
      if (item == shortcuts.end()) continue;
      if (item->native_pending) {
        if (status.ok) status = result::failure("shortcut_cleanup_pending", "A native shortcut operation has not returned yet.");
        continue;
      }
      item->native_pending = true;
      const auto cleared = native->unregister_global_shortcut(selected);
      // Native cleanup can synchronously close or reconfigure another window.
      item = std::find_if(shortcuts.begin(), shortcuts.end(), [selected](const auto& value) {
        return value.token == selected;
      });
      if (item != shortcuts.end()) {
        item->native_pending = false;
        if (cleared.ok) shortcuts.erase(item);
      }
      if (!cleared.ok && status.ok) status = cleared;
    }
    return status;
  }
  void on_closed() noexcept {
    accepting.store(false);
    revoke_shortcuts();
    resources.stop();
    asset_channel->revoke();
    cancel_assets();
    pending_close_request = false;
    ready = false;
    token.clear();
    ++generation;
    if (!notified_closed) {
      ++group->window_revision;
      notified_closed = true;
      pending_closed = true;
      drain();
    }
    group->stop_if_empty();
  }
  bool allowed(std::string_view uri) const {
    return accepting.load() && !token.empty() && detail::same_document_uri(uri, current.uri);
  }
  result deliver(nlohmann::json value) {
    if (!accepting.load() || token.empty()) return unavailable();
    value["v"] = 1;
    value["token"] = token;
    const auto text = value.dump();
    if (text.size() > detail::maximum_response_bytes) {
      return result::failure("response_too_large", "Native Bridge response exceeds its byte limit.");
    }
    auto sent = native->evaluate("window.__souiDesktopDeliver&&window.__souiDesktopDeliver(JSON.parse(" +
      nlohmann::json(text).dump() + "));");
    return sent;
  }
  file_selection_result choose_files(const open_file_options& settings) {
    open_file_options normalized;
    const auto valid = normalize_file_options(settings.title, settings.extensions, normalized.extensions);
    if (!valid.ok) return {valid, {}};
    normalized.title = settings.title;
    normalized.multiple = settings.multiple;
    file_selection_result selected;
    {
      callback_scope scope(group->callback_depth);
      try { selected = native->choose_files(normalized); }
      catch (...) { selected.status = result::failure("service_failed", "The native file open dialog failed."); }
    }
    if (!accepting.load()) return {unavailable(), {}};
    if (!selected.status.ok) return {std::move(selected.status), {}};
    selected.status = validate_file_paths(selected.paths, normalized.multiple);
    if (!selected.status.ok) selected.paths.clear();
    return selected;
  }
  value_result choose_save_file(const save_file_options& settings) {
    save_file_options normalized;
    const auto valid = normalize_file_options(settings.title, settings.extensions, normalized.extensions);
    if (!valid.ok) return {valid, {}};
    if (!portable_basename(settings.default_name)) return {invalid_file_options(), {}};
    normalized.title = settings.title;
    normalized.default_name = settings.default_name;
    value_result selected;
    {
      callback_scope scope(group->callback_depth);
      try { selected = native->choose_save_file(normalized); }
      catch (...) { selected.status = result::failure("service_failed", "The native file save dialog failed."); }
    }
    if (!accepting.load()) return {unavailable(), {}};
    if (!selected.status.ok) return {std::move(selected.status), {}};
    if (!local_absolute_path(selected.value))
      return {result::failure("invalid_result", "Native file dialog returned an invalid, oversized or nonlocal path."), {}};
    return selected;
  }
  result invoke_service(std::string_view method, const nlohmann::json& params,
                        nlohmann::json& returned) {
    const bool read = method == "desktop.clipboard.readText";
    const bool write = method == "desktop.clipboard.writeText";
    const bool directory = method == "desktop.dialog.chooseDirectory";
    const bool open_file = method == "desktop.dialog.openFiles";
    const bool save_file = method == "desktop.dialog.saveFile";
    const bool permitted = read ? settings.services.clipboard_read :
      write ? settings.services.clipboard_write : directory ? settings.services.directory_dialog :
      open_file ? settings.services.file_open_dialog : save_file ? settings.services.file_save_dialog :
      settings.services.window_properties;
    if (!permitted) return result::failure("permission_denied", "This desktop service is not enabled.");
    const auto invalid = result::failure("invalid_params", "Desktop service parameters have invalid fields, types or bounds.");
    if (!params.is_object()) return invalid;
    // Native services can run a modal message loop. Queue all business callbacks
    // until the native operation returns, including when it throws.
    callback_scope scope(group->callback_depth);
    result status;
    if (open_file || save_file) {
      open_file_options open;
      save_file_options save;
      if (!parse_file_options(params, save_file, open.title, open.extensions, open.multiple, save.default_name))
        return invalid;
      if (open_file) {
        auto selected = choose_files(open);
        if (!selected.status.ok && selected.status.code == "E_CANCELLED")
          returned = {{"cancelled", true}, {"paths", nlohmann::json::array()}};
        else if (!selected.status.ok) return selected.status;
        else returned = {{"cancelled", false}, {"paths", std::move(selected.paths)}};
      } else {
        save.title = std::move(open.title);
        save.extensions = std::move(open.extensions);
        auto selected = choose_save_file(save);
        if (!selected.status.ok && selected.status.code == "E_CANCELLED")
          returned = {{"cancelled", true}, {"path", ""}};
        else if (!selected.status.ok) return selected.status;
        else returned = {{"cancelled", false}, {"path", std::move(selected.value)}};
      }
      return result::success();
    } else if (method == "desktop.window.setTitle" || write) {
      const char* field = write ? "text" : "title";
      if (params.size() != 1 || !params.contains(field) || !params.at(field).is_string()) return invalid;
      const auto& text = params.at(field).get_ref<const std::string&>();
      if (!valid_text(text, write ? 32768 : 4096)) return invalid;
      status = write ? native->clipboard_write(text) : native->set_title(text);
    } else if (method == "desktop.window.resize") {
      if (params.size() != 2 || !params.contains("width") || !params.contains("height")) return invalid;
      for (const char* field : {"width", "height"}) {
        const auto& value = params.at(field);
        if (!value.is_number_integer() || value < 64 || value > 16384) return invalid;
      }
      status = native->resize(params.at("width").get<int>(), params.at("height").get<int>());
    } else if (method == "desktop.window.show") {
      if (params.size() != 1 || !params.contains("visible") || !params.at("visible").is_boolean()) return invalid;
      status = native->show(params.at("visible").get<bool>());
    } else {
      if (!params.empty()) return invalid;
      if (read || directory) {
        auto value = read ? native->clipboard_read() : native->choose_directory();
        if (directory && !value.status.ok && value.status.code == "E_CANCELLED") {
          returned = {{"cancelled", true}, {"path", ""}};
          return result::success();
        }
        if (!value.status.ok) return value.status;
        if (!valid_text(value.value, 65536) || (directory && value.value.empty()))
          return result::failure("invalid_result", "Native desktop service returned invalid or oversized UTF-8 text.");
        returned = read ? nlohmann::json{{"text", value.value}} :
          nlohmann::json{{"cancelled", false}, {"path", value.value}};
        return result::success();
      }
      status = native->focus();
    }
    if (status.ok) returned = {{"accepted", true}};
    return status;
  }
  void receive(std::string_view payload, std::string_view source, detail::message_frame frame) {
    if (!accepting.load() || !allowed(source) ||
        frame == detail::message_frame::subframe ||
        (frame == detail::message_frame::unknown && native->has_message_frame_metadata())) return;
    if (group->callback_depth) {
      if (payload.size() <= detail::maximum_request_bytes && source.size() <= 65536 && requests.size() < 128)
        requests.push_back({std::string(payload), std::string(source), frame});
      return;
    }
    try {
      const auto input = detail::parse_bounded_json(payload, detail::maximum_request_bytes);
      if (!input.is_object() || input.size() != 5 ||
          !input.contains("v") || input.at("v") != 1 ||
          !input.contains("token") || !input.at("token").is_string() ||
          input.at("token").get_ref<const std::string&>() != token ||
          !input.contains("id") || !input.at("id").is_number_unsigned() ||
          !input.contains("method") || !input.at("method").is_string() ||
          !input.contains("params")) return;
      const auto id = input.at("id").get<std::uint64_t>();
      const auto& method = input.at("method").get_ref<const std::string&>();
      if (id <= last_request || id > detail::maximum_request_id || !detail::valid_name(method)) return;
      last_request = id;
      const auto request_generation = generation;
      nlohmann::json response{{"id", id}};
      if (desktop_service(method)) {
        result status;
        try { status = invoke_service(method, input.at("params"), response["result"]); }
        catch (...) { status = result::failure("service_failed", "The native desktop service failed."); }
        response["ok"] = status.ok;
        if (!status.ok) {
          response.erase("result");
          response["error"] = {{"code", status.code}, {"message", status.message}};
        }
      } else if (method == "desktop.window.minimize" || method == "desktop.window.maximize" ||
          method == "desktop.window.restore" || method == "desktop.window.getState") {
        const auto& params = input.at("params");
        result status = result::failure("permission_denied", "Window controls are not enabled.");
        if (settings.enable_window_controls) {
          if (!params.is_object() || !params.empty()) {
            status = result::failure("invalid_params", "Window controls require an empty object.");
          } else if (method == "desktop.window.getState") {
            const auto observed = native->get_window_state();
            status = observed.status;
            if (status.ok) response["result"] = {{"minimized", observed.value.minimized},
              {"maximized", observed.value.maximized}};
          } else {
            if (method == "desktop.window.minimize") status = native->minimize();
            else if (method == "desktop.window.maximize") status = native->maximize();
            else status = native->restore();
            if (status.ok) response["result"] = {{"accepted", true}};
          }
        }
        response["ok"] = status.ok;
        if (!status.ok) response["error"] = {{"code", status.code}, {"message", status.message}};
      } else if (method == "soui.asset.read") {
        const auto& params = input.at("params");
        result status = result::failure("asset_read_disabled", "Local asset reading is not enabled.");
        if (settings.enable_asset_read) {
          if (!params.is_object() || params.size() != 1 ||
              !params.contains("path") || !params.at("path").is_string()) {
            status = result::failure("invalid_asset_path", "Expected exactly one asset path string.");
          } else {
            status = submit_asset(id, params.at("path").get_ref<const std::string&>());
            if (status.ok) { drain(); return; }
          }
        }
        response["ok"] = status.ok;
        if (!status.ok) response["error"] = {{"code", status.code}, {"message", status.message}};
      } else if (!handlers.invoke) {
        response["ok"] = false;
        response["error"] = {{"code", "method_not_registered"}, {"message", "No native method handler is registered."}};
      } else {
        try {
          ++group->callback_depth;
          std::string returned;
          try { returned = handlers.invoke(method, input.at("params").dump()); }
          catch (...) { --group->callback_depth; throw; }
          --group->callback_depth;
          response["result"] = detail::parse_bounded_json(returned, detail::maximum_response_bytes / 2);
          response["ok"] = true;
        } catch (...) {
          response["ok"] = false;
          response["error"] = {{"code", "handler_failed"}, {"message", "The application rejected this request."}};
        }
      }
      // A handler can close or navigate. Never send its result to a later page.
      if (generation == request_generation && accepting.load()) {
        auto sent = deliver(std::move(response));
        if (!sent.ok) report(std::move(sent));
      }
      drain();
    } catch (...) {
      // Invalid/untrusted envelopes have no right to obtain an error response.
      report(result::failure("invalid_bridge_message", "Discarded an invalid or oversized Bridge envelope."));
    }
  }
  result navigate(const std::filesystem::path& relative) {
    const auto usable = check();
    if (!usable.ok) return usable;
    detail::document next;
    const auto resolved = detail::resolve_document(settings.resource_root, relative, next);
    if (!resolved.ok) return resolved;
    if (settings.enable_resource_server)
      next.uri = detail::resource_path_uri(resources.base_uri(),
        next.path.lexically_relative(settings.resource_root).generic_u8string());
    unsigned char bytes[32]{};
    cancel_assets();
    if (settings.enable_asset_read) asset_session = std::make_shared<std::atomic<bool>>(false);
    token.clear();
    ready = false;
    ++generation;
    last_request = 0;
    if (!soui::runtime::detail::secure_random(bytes, sizeof(bytes)))
      return result::failure("entropy_unavailable", "Cannot authorize a document without system entropy.");
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) { token += digits[byte >> 4U]; token += digits[byte & 15U]; }
    current = std::move(next);
    auto loaded = native->load(current.uri, detail::make_preload(token));
    if (!loaded.ok) token.clear();
    return loaded;
  }
  result close() {
    if (!initialized) return unavailable();
    resources.stop();
    asset_channel->revoke();
    cancel_assets();
    if (accepting.exchange(false)) ++group->window_revision;
    pending_close_request = false;
    ready = false;
    token.clear();
    ++generation;
    result cleared;
    result closed;
    {
      callback_scope scope(group->callback_depth);
      cleared = cleanup_shortcuts();
      closed = native->close();
    }
    drain();
    return closed.ok ? cleared : closed;
  }
  bool approve_close() noexcept {
    bool approved = true;
    ++group->callback_depth;
    try {
      const auto handler = handlers.close_requested;
      if (handler) approved = handler(identity);
    }
    catch (...) {
      approved = false;
      report(result::failure("close_callback_failed", "The close approval callback failed."));
    }
    --group->callback_depth;
    return approved;
  }
  result request_close() {
    const auto checked = check();
    if (!checked.ok) return checked;
    if (group->approving_close || group->close_error_callback)
      return result::failure("close_busy", "Close approval is already in progress.");
    if (group->callback_depth) {
      pending_close_request = true;
      return result::success();
    }
    pending_close_request = false;
    group->approving_close = true;
    const bool approved = approve_close();
    // A handler's explicit force-close remains effective, even if it vetoes.
    auto status = approved ? (accepting.load() ? close() : result::success()) :
      result::failure("close_cancelled", "The application cancelled the close request.");
    group->approving_close = false;
    drain();
    return status;
  }
};

std::size_t application_group::open_count() const {
  std::size_t count = 0;
  for (const auto& weak : windows)
    if (const auto state = weak.lock(); state && state->initialized && !state->notified_closed) ++count;
  return count;
}
std::size_t application_group::reserved_count() const {
  std::size_t count = 0;
  for (const auto& weak : windows)
    if (const auto state = weak.lock(); state && state->native && !state->notified_closed) ++count;
  return count;
}
void application_group::drain() noexcept {
  if (draining || callback_depth || approving_close) return;
  draining = true;
  bool progressed;
  do {
    progressed = false;
    const auto snapshot = windows; // A callback can create another window.
    for (const auto& weak : snapshot) if (const auto state = weak.lock(); state && !state->draining) {
      if (pending_quit || state->pending_close_request || state->pending_closed || state->pending_ready || state->pending_state_changed || !state->errors.empty() ||
          !state->commands.empty() || !state->shortcut_events.empty() ||
          !state->requests.empty() || !state->work_items.empty()) {
        progressed = true;
        state->drain();
      }
    }
  } while (progressed && !callback_depth);
  draining = false;
}
void application_group::stop_if_empty() noexcept {
  if (!seen_window || stopped || open_count()) return;
  stopped = true;
  quitting = true;
  if (const auto owner = root.lock(); owner && owner->native) {
    try {
      const auto stopped_result = owner->native->quit_loop();
      if (!stopped_result.ok) owner->report(stopped_result);
    } catch (...) {}
  }
}
result application_group::quit() {
  quitting = true;
  pending_quit = false;
  result status = result::success();
  const auto snapshot = windows;
  for (const auto& weak : snapshot) if (const auto state = weak.lock();
      state && state->initialized && !state->notified_closed) {
    const auto closed = state->close();
    if (!closed.ok && status.ok) status = closed;
  }
  stop_if_empty();
  return status;
}
result application_group::request_quit() {
  if (approving_close || close_error_callback)
    return result::failure("close_busy", "Close approval is already in progress.");
  if (quitting || !open_count()) return unavailable();
  if (callback_depth) {
    pending_quit = true;
    return result::success();
  }
  pending_quit = false;
  std::vector<std::shared_ptr<application_state>> snapshot;
  for (const auto& weak : windows)
    if (const auto state = weak.lock(); state && state->accepting.load()) snapshot.push_back(state);
  const auto revision = window_revision;
  approving_close = true;
  bool approved = true;
  // Coalesce older per-window requests into this all-or-nothing preflight.
  for (const auto& state : snapshot) state->pending_close_request = false;
  for (const auto& state : snapshot) {
    if (!state->approve_close() || revision != window_revision || quitting) {
      approved = false;
      break;
    }
  }
  auto status = approved ? quit() :
    result::failure("close_cancelled", "The application cancelled quit or changed its windows.");
  approving_close = false;
  drain();
  return status;
}
}  // namespace

struct application::implementation {
  std::shared_ptr<application_state> state = std::make_shared<application_state>();
};

application::application(options settings, callbacks handlers) : impl_(std::make_unique<implementation>()) {
  impl_->state->group = std::make_shared<application_group>();
  impl_->state->group->root = impl_->state;
  impl_->state->settings = std::move(settings);
  impl_->state->handlers = std::move(handlers);
}
application::application(std::unique_ptr<implementation> value) : impl_(std::move(value)) {}
application::~application() {
  auto state = impl_->state;
  // Native UI objects cannot be safely destroyed from a worker. This is a
  // documented contract, not a silent cross-thread teardown or leaked owner.
  if (state->native && std::this_thread::get_id() != state->owner) std::terminate();
  state->handlers = {};
  if (state->is_root) {
    for (const auto& weak : state->group->windows)
      if (const auto child = weak.lock()) child->handlers = {};
    (void)state->group->quit();
  }
  state->accepting.store(false);
  state->resources.stop();
  state->asset_channel->revoke();
  state->cancel_assets();
  state->token.clear();
  if (state->native) {
    (void)state->cleanup_shortcuts();
    (void)state->native->close();
    state->native.reset();
  }
  if (state->owns_process_slot) process_application.store(false);
}

result application::initialize() {
  auto state = impl_->state;
  const auto thread = state->check_thread();
  if (!thread.ok) return thread;
  if (state->initialized || state->native) return result::failure("already_initialized", "Initialize once per application.");
  if (!valid_text(state->settings.title, 4096) || state->settings.width < 64 || state->settings.height < 64 ||
      state->settings.width > 16384 || state->settings.height > 16384)
    return result::failure("invalid_options", "Window title or dimensions are invalid.");
  detail::document initial;
  auto verified = detail::resolve_document(state->settings.resource_root, state->settings.entry, initial);
  if (!verified.ok) return verified;
  std::error_code error;
  state->settings.resource_root = std::filesystem::canonical(state->settings.resource_root, error);
  if (error) return result::failure("invalid_root", "Cannot canonicalize the resource root.");
  if (state->group->quitting) return unavailable();
  if (state->is_root) {
    bool expected = false;
    if (!process_application.compare_exchange_strong(expected, true))
      return result::failure("application_exists", "Only one desktop application group is supported per process.");
    state->owns_process_slot = true;
  }
  auto identity = next_window_id.load();
  do {
    if (identity == std::numeric_limits<window_id>::max())
      return result::failure("window_id_exhausted", "Window identifiers cannot be reused.");
  } while (!next_window_id.compare_exchange_weak(identity, identity + 1));
  state->identity = identity;
  auto& windows = state->group->windows;
  windows.erase(std::remove_if(windows.begin(), windows.end(), [](const auto& weak) { return weak.expired(); }), windows.end());
  windows.push_back(state);
  ++state->group->window_revision;
  state->native = detail::make_backend();
  if (!state->native) return result::failure("not_supported", "No desktop backend was built.");
  state->asset_channel->target = state->native.get();
  if (state->settings.enable_resource_server) {
    const auto started = state->resources.start(state->settings.resource_root);
    if (!started.ok) return started;
  }
  const std::weak_ptr<application_state> weak = state;
  detail::backend_events events;
  events.allow_navigation = [weak](std::string_view uri) {
    const auto live = weak.lock(); return live && live->allowed(uri);
  };
  events.message = [weak](std::string_view payload, std::string_view uri, detail::message_frame frame) {
    if (const auto live = weak.lock()) live->receive(payload, uri, frame);
  };
  events.loaded = [weak](std::string_view uri) {
    if (const auto live = weak.lock(); live && live->allowed(uri)) {
      live->ready = true;
      live->pending_ready = true;
      live->ready_generation = live->generation;
      live->drain();
    }
  };
  events.closed = [weak]() { if (const auto live = weak.lock()) live->on_closed(); };
  events.state_changed = [weak](window_state value) {
    if (const auto live = weak.lock()) live->on_state_changed(value);
  };
  events.command = [weak](std::string_view command) { if (const auto live = weak.lock()) live->on_command(command); };
  events.global_shortcut = [weak](std::uint64_t token) {
    if (const auto live = weak.lock()) live->on_global_shortcut(token);
  };
  events.close_requested = [weak]() { if (const auto live = weak.lock()) (void)live->request_close(); };
  events.quit_requested = [weak]() { if (const auto live = weak.lock()) (void)live->group->request_quit(); };
  events.reload = [weak]() {
    if (const auto live = weak.lock(); live && live->accepting.load()) {
      auto restarted = live->navigate(live->current.path.lexically_relative(live->settings.resource_root));
      if (!restarted.ok) live->report(std::move(restarted));
    }
  };
  events.error = [weak](result failure) { if (const auto live = weak.lock()) live->report(std::move(failure)); };
  auto created = state->native->create(state->settings, std::move(events));
  if (!created.ok) { state->resources.stop(); return created; }
  state->initialized = true;
  state->group->seen_window = true;
  // Native creation may pump a nested UI loop. A quit issued there cannot see
  // this not-yet-initialized window in open_count(), so finish its teardown
  // rather than resurrecting a window after the application's loop stopped.
  if (state->group->quitting) {
    (void)state->close();
    state->group->stop_if_empty();
    return result::failure("closed", "The application quit during native window initialization.");
  }
  if (state->notified_closed) {
    state->group->stop_if_empty();
    return result::failure("closed", "The native window closed during initialization.");
  }
  state->accepting.store(true);
  auto navigation = state->navigate(state->settings.entry);
  if (!navigation.ok) { state->accepting.store(false); (void)state->native->close(); }
  return navigation;
}
result application::run() {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return checked;
  if (!state->is_root) return result::failure("not_loop_owner", "Run the root application event loop.");
  if (!state->initialized || !state->group->open_count()) return unavailable();
  if (state->group->loop_started) return result::failure("already_running", "The application event loop runs only once.");
  state->group->loop_started = true;
  return state->native->run();
}
result application::close() {
  auto state = impl_->state;
  const auto thread = state->check_thread();
  if (!thread.ok) return thread;
  return state->close();
}
result application::quit() {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return checked;
  if (!state->initialized) return unavailable();
  return state->group->quit();
}
result application::request_close() {
  auto state = impl_->state;
  return state->request_close();
}
result application::request_quit() {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return checked;
  return state->initialized ? state->group->request_quit() : unavailable();
}
window_result application::create_window(options settings, callbacks handlers) {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return {checked, {}};
  if (!state->initialized || state->group->quitting || !state->group->open_count())
    return {unavailable(), {}};
  // Even an unsuccessful create attempt invalidates a running quit preflight.
  ++state->group->window_revision;
  // Native create() may enter a nested UI loop before initialized becomes true.
  // Count those reserved slots too, without treating them as live loop owners.
  if (state->group->reserved_count() >= 32)
    return {result::failure("window_limit", "The application supports at most 32 live windows."), {}};
  auto value = std::make_unique<implementation>();
  value->state->settings = std::move(settings);
  value->state->handlers = std::move(handlers);
  value->state->group = state->group;
  value->state->is_root = false;
  auto child = std::shared_ptr<application>(new application(std::move(value)));
  auto created = child->initialize();
  if (!created.ok) return {std::move(created), {}};
  return {result::success(), std::move(child)};
}
window_id application::id() const noexcept { return impl_->state->identity; }
std::vector<window_id> application::window_ids() const {
  const auto state = impl_->state;
  if (!state->check_thread().ok) return {};
  std::vector<window_id> ids;
  for (const auto& weak : state->group->windows)
    if (const auto member = weak.lock(); member && member->accepting.load()) ids.push_back(member->identity);
  return ids;
}
result application::navigate(const std::filesystem::path& relative) { return impl_->state->navigate(relative); }
result application::reload() {
  auto state = impl_->state;
  const auto checked = state->check();
  if (!checked.ok) return checked;
  return state->navigate(state->current.path.lexically_relative(state->settings.resource_root));
}
result application::emit(std::string_view event, std::string_view value) {
  auto state = impl_->state;
  const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!state->ready) return result::failure("document_not_ready", "Wait for the ready callback before emitting events.");
  if (!detail::valid_name(event)) return result::failure("invalid_event", "Event name must be bounded ASCII.");
  try {
    return state->deliver({{"event", event}, {"value", detail::parse_bounded_json(value, detail::maximum_response_bytes / 2)}});
  } catch (...) { return result::failure("invalid_json", "Event value is not bounded JSON."); }
}
result application::dispatch(std::function<void()> work) {
  auto state = impl_->state;
  if (!work) return result::failure("invalid_callback", "A work callback is required.");
  if (!state->accepting.load()) return unavailable();
  if (state->pending_work.fetch_add(1) >= 1024) {
    state->pending_work.fetch_sub(1);
    return result::failure("queue_full", "The application dispatch queue is full.");
  }
  const std::weak_ptr<application_state> weak = state;
  auto queued = state->native->dispatch([weak, work = std::move(work)]() mutable {
    if (const auto live = weak.lock(); live && live->accepting.load()) {
      live->work_items.push_back(std::move(work));
      live->drain();
    }
  });
  if (!queued.ok) state->pending_work.fetch_sub(1);
  return queued;
}
result application::set_title(std::string_view title) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!valid_text(title, 4096)) return result::failure("invalid_title", "Title must be bounded UTF-8.");
  return state->native->set_title(title);
}
result application::resize(int width, int height) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (width < 64 || height < 64 || width > 16384 || height > 16384)
    return result::failure("invalid_size", "Window dimensions must be between 64 and 16384.");
  return state->native->resize(width, height);
}
result application::show(bool visible) {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->show(visible) : checked;
}
result application::focus() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->focus() : checked;
}
result application::minimize() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->minimize() : checked;
}
result application::maximize() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->maximize() : checked;
}
result application::restore() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->restore() : checked;
}
window_state_result application::get_window_state() const {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->get_window_state() : window_state_result{checked, {}};
}
value_result application::clipboard_read() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->clipboard_read() : value_result{checked, {}};
}
result application::clipboard_write(std::string_view text) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!valid_text(text, 1048576)) return result::failure("invalid_text", "Clipboard text must be bounded UTF-8.");
  return state->native->clipboard_write(text);
}
value_result application::choose_directory() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->choose_directory() : value_result{checked, {}};
}
file_selection_result application::choose_files(const open_file_options& settings) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return {checked, {}};
  auto selected = state->choose_files(settings);
  state->drain();
  return state->accepting.load() ? std::move(selected) : file_selection_result{unavailable(), {}};
}
value_result application::choose_save_file(const save_file_options& settings) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return {checked, {}};
  auto selected = state->choose_save_file(settings);
  state->drain();
  return state->accepting.load() ? std::move(selected) : value_result{unavailable(), {}};
}
result application::popup_menu(const std::vector<menu_item>& items) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  const auto valid = validate_menu(items);
  if (!valid.ok) return valid;
  auto previous = std::move(state->popup_commands);
  // Backends validate their native menu generation before entering common code.
  // A command deferred by another window's callback must not outlive a later
  // action reconfiguration, even when the new menu reuses the same command ID.
  // Conservatively invalidate both popup and tray queued commands per window.
  ++state->action_revision;
  const auto revision = ++state->popup_revision;
  state->popup_commands = enabled_commands(items);
  auto shown = state->native->popup_menu(items);
  if (!shown.ok && state->popup_revision == revision) {
    state->popup_commands = std::move(previous);
    ++state->action_revision;
  }
  return shown;
}
result application::set_tray(const tray_options& settings) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  const auto valid = validate_menu(settings.menu);
  if (!valid.ok) return valid;
  if (settings.label.empty() || !valid_text(settings.label, 32) || !valid_text(settings.tooltip, 4096))
    return result::failure("invalid_tray", "Tray label and tooltip must be bounded UTF-8.");
  auto previous = std::move(state->tray_commands);
  ++state->action_revision;
  const auto revision = ++state->tray_revision;
  state->tray_commands = enabled_commands(settings.menu);
  auto changed = state->native->set_tray(settings);
  if (!changed.ok && state->tray_revision == revision) {
    state->tray_commands = std::move(previous);
    ++state->action_revision;
  }
  return changed;
}
result application::clear_tray() {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  ++state->action_revision;
  const auto revision = ++state->tray_revision;
  auto cleared = state->native->clear_tray();
  if (state->tray_revision == revision) {
    if (cleared.ok) state->tray_commands.clear();
    else ++state->action_revision;
  }
  return cleared;
}
result application::register_global_shortcut(std::string_view id, const global_shortcut& shortcut) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!detail::valid_name(id))
    return result::failure("invalid_shortcut_id", "Shortcut IDs must be bounded ASCII names.");
  global_shortcut normalized;
  const auto valid = normalize_shortcut(shortcut, normalized);
  if (!valid.ok) return valid;
  for (const auto& item : state->shortcuts) {
    if (item.id == id)
      return result::failure("duplicate_shortcut_id", "This shortcut ID already has a registration or pending cleanup.");
    if (item.chord.key == normalized.key && item.chord.modifiers == normalized.modifiers)
      return result::failure("duplicate_shortcut", "This shortcut combination is already registered or awaiting cleanup.");
  }
  if (state->shortcuts.size() >= 32)
    return result::failure("shortcut_limit", "Each window supports at most 32 registrations including pending cleanup.");
  auto selected = next_shortcut_token.load();
  do {
    if (selected == std::numeric_limits<std::uint64_t>::max())
      return result::failure("shortcut_id_exhausted", "Shortcut identifiers cannot be reused.");
  } while (!next_shortcut_token.compare_exchange_weak(selected, selected + 1));
  state->shortcuts.push_back({std::string(id), normalized, selected, false, true});
  result registered;
  {
    callback_scope scope(state->group->callback_depth);
    registered = state->native->register_global_shortcut(selected, normalized);
    auto item = std::find_if(state->shortcuts.begin(), state->shortcuts.end(), [selected](const auto& value) {
      return value.token == selected;
    });
    if (item != state->shortcuts.end()) item->native_pending = false;
    if (!registered.ok) {
      if (registered.code != "shortcut_cleanup_pending" && item != state->shortcuts.end())
        state->shortcuts.erase(item);
    } else if (state->accepting.load() && item != state->shortcuts.end() && !item->revoked) item->active = true;
    else {
      // A nested native operation can revoke or close a pending registration.
      if (item != state->shortcuts.end()) item->native_pending = true;
      const auto cleared = state->native->unregister_global_shortcut(selected);
      item = std::find_if(state->shortcuts.begin(), state->shortcuts.end(), [selected](const auto& value) {
        return value.token == selected;
      });
      if (item != state->shortcuts.end()) {
        item->native_pending = false;
        if (cleared.ok) state->shortcuts.erase(item);
      }
      registered = !state->accepting.load() ? unavailable() : cleared.ok ?
        result::failure("shortcut_cancelled", "Shortcut registration was revoked before it completed.") :
        result::failure("shortcut_cleanup_pending", cleared.message);
    }
  }
  state->drain();
  return registered;
}
result application::unregister_global_shortcut(std::string_view id) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!detail::valid_name(id))
    return result::failure("invalid_shortcut_id", "Shortcut IDs must be bounded ASCII names.");
  auto item = std::find_if(state->shortcuts.begin(), state->shortcuts.end(), [id](const auto& value) {
    return value.id == id;
  });
  if (item == state->shortcuts.end()) return result::success();
  item->active = false;
  item->revoked = true;
  if (item->native_pending)
    return result::failure("shortcut_cleanup_pending", "A native shortcut operation has not returned yet.");
  const auto selected = item->token;
  item->native_pending = true;
  result cleared;
  {
    callback_scope scope(state->group->callback_depth);
    cleared = state->native->unregister_global_shortcut(selected);
    item = std::find_if(state->shortcuts.begin(), state->shortcuts.end(), [selected](const auto& value) {
      return value.token == selected;
    });
    if (item != state->shortcuts.end()) {
      item->native_pending = false;
      if (cleared.ok) state->shortcuts.erase(item);
    }
  }
  state->drain();
  return cleared;
}
capabilities application::get_capabilities() const {
  const auto state = impl_->state;
  if (!state->check_thread().ok || !state->native) return {};
  return state->native->get_capabilities();
}
}  // namespace soui::desktop
