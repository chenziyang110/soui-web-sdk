#include "../backend.h"
#include "linux_runtime_preflight.h"
#if __has_include("webkit_runtime_config.h")
#include "webkit_runtime_config.h"
#else
// Standalone backend builds retain the unknown/default deployment behavior.
#define SOUI_DESKTOP_WEBKIT_HELPER_DIR ""
#endif
#include "../resource_uri.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace soui::desktop::detail {
namespace {

constexpr std::size_t max_text_bytes = 1024 * 1024;
constexpr std::size_t max_bridge_bytes = 64 * 1024;
constexpr guint clipboard_timeout_ms = 2000;
constexpr std::size_t max_pending_dispatches = 1024;
constexpr guint tray_embed_timeout_ms = 1500;

struct linux_state : std::enable_shared_from_this<linux_state> {
  std::thread::id owner{std::this_thread::get_id()};
  std::atomic<bool> active{false};
  backend_events events;
  GtkWidget* window{nullptr};
  WebKitWebView* view{nullptr};
  WebKitUserContentManager* content{nullptr};
  WebKitWebContext* web_context{nullptr};
  WebKitWebsiteDataManager* data{nullptr};
  GCancellable* evaluations{nullptr};
  GMainContext* context{g_main_context_ref_thread_default()};
  GMainLoop* loop{g_main_loop_new(context, FALSE)};
  GtkWidget* directory_dialog{nullptr};  // Borrowed while its modal call is active.
  GMainLoop* clipboard_loop{nullptr};    // Borrowed while its bounded call is active.
  GtkWidget* popup{nullptr};
  GtkWidget* tray_menu{nullptr};
  GtkStatusIcon* tray_icon{nullptr};
  GMainLoop* tray_wait_loop{nullptr};    // Borrowed while confirming native embedding.
  std::mutex dispatch_mutex;
  std::vector<GSource*> dispatch_sources;
  std::uint64_t generation{0};
  bool running{false};
  bool committed{false};
  bool load_failed{false};
  bool modal_busy{false};
  bool created{false};
  bool initialized{false};
  bool enable_resource_server{false};
  bool loop_quit_requested{false};
  bool navigation_ticket{false};
  bool renderer_reload_pending{false};
  bool tray_was_embedded{false};
  std::uint64_t popup_generation{0};
  std::uint64_t tray_generation{0};

  ~linux_state() {
    g_main_loop_unref(loop);
    g_main_context_unref(context);
  }
};

result unavailable() {
  return result::failure("E_CLOSED", "The Linux desktop window is not open.");
}

result check_ui(const linux_state& state) {
  if (state.owner != std::this_thread::get_id())
    return result::failure("E_THREAD", "This operation requires the desktop UI thread.");
  if (!state.active.load()) return unavailable();
  return result::success();
}

bool valid_text(std::string_view text, std::size_t limit = max_text_bytes) {
  if (text.empty()) return true;
  return text.size() <= limit && text.find('\0') == std::string_view::npos &&
         g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr);
}

result enqueue(linux_state& state, std::function<void()> work);

void clear_menu(GtkWidget*& slot) noexcept {
  GtkWidget* menu = std::exchange(slot, nullptr);
  if (!menu) return;
  gtk_menu_popdown(GTK_MENU(menu));
  gtk_widget_destroy(menu);
  g_object_unref(menu);
}

// GTK3's only built-in tray implementation is the deprecated XEmbed
// GtkStatusIcon. Keep that compatibility use isolated; do not suppress other
// deprecated API diagnostics or pretend it implements Wayland StatusNotifier.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void clear_native_tray(linux_state& state) noexcept {
  ++state.tray_generation;
  state.tray_was_embedded = false;
  if (state.tray_wait_loop) g_main_loop_quit(state.tray_wait_loop);
  if (auto* icon = std::exchange(state.tray_icon, nullptr)) {
    g_signal_handlers_disconnect_by_data(icon, &state);
    gtk_status_icon_set_visible(icon, FALSE);
    g_object_unref(icon);
  }
  clear_menu(state.tray_menu);
}
G_GNUC_END_IGNORE_DEPRECATIONS

void report(linux_state& state, result status) noexcept {
  if (!state.active.load()) return;
  try {
    auto handler = state.events.error;
    if (handler) handler(std::move(status));
  } catch (...) {
    // No C++ exception may cross a GTK/WebKit C callback boundary.
  }
}

bool admissible(linux_state& state, const char* uri) noexcept {
  if (!state.active.load() || !uri || !state.events.allow_navigation) return false;
  const std::string_view candidate(uri);
  // A loopback-shaped URL alone is not authority: common code must also
  // recognize this window's current native-selected document.
  if (candidate.substr(0, 7) != "file://" &&
      !(state.enable_resource_server && is_resource_uri(candidate))) return false;
  try {
    auto handler = state.events.allow_navigation;
    return handler(uri);
  } catch (...) {
    return false;
  }
}

void shutdown(linux_state& state, bool notify, bool window_destroyed = false) noexcept {
  const bool created = state.created;
  state.created = false;
  if (!state.active.exchange(false) && !created) return;
  std::function<void()> on_closed;
  if (notify) on_closed = std::move(state.events.closed);
  state.events = {};
  ++state.generation;

  std::vector<GSource*> sources;
  {
    std::lock_guard<std::mutex> lock(state.dispatch_mutex);
    sources.swap(state.dispatch_sources);
  }
  for (auto* source : sources) {
    g_source_destroy(source);
    g_source_unref(source);
  }
  if (state.evaluations) g_cancellable_cancel(state.evaluations);
  if (state.clipboard_loop) g_main_loop_quit(state.clipboard_loop);
  if (state.directory_dialog) gtk_widget_destroy(state.directory_dialog);
  ++state.popup_generation;
  clear_menu(state.popup);
  clear_native_tray(state);
  // Window teardown never stops the group's UI loop. The common owner invokes
  // quit_loop only when its last live window closes or explicit quit completes.

  // Disconnect before stopping/destroying widgets: those operations emit signals.
  if (state.content) {
    g_signal_handlers_disconnect_by_data(state.content, &state);
    webkit_user_content_manager_unregister_script_message_handler(state.content, "soui");
    webkit_user_content_manager_remove_all_scripts(state.content);
  }
  if (state.web_context) g_signal_handlers_disconnect_by_data(state.web_context, &state);
  if (state.view) {
    g_signal_handlers_disconnect_by_data(state.view, &state);
    if (!window_destroyed) webkit_web_view_stop_loading(state.view);
  }
  if (state.window) {
    g_signal_handlers_disconnect_by_data(state.window, &state);
    if (!window_destroyed) gtk_widget_destroy(state.window);
  }
  g_clear_object(&state.view);
  g_clear_object(&state.window);
  g_clear_object(&state.content);
  g_clear_object(&state.web_context);
  g_clear_object(&state.data);
  g_clear_object(&state.evaluations);
  if (on_closed) {
    try { on_closed(); } catch (...) {}
  }
}

gboolean window_delete(GtkWidget*, GdkEvent*, gpointer data) noexcept {
  shutdown(*static_cast<linux_state*>(data), true);
  return TRUE;
}

void window_destroy(GtkWidget*, gpointer data) noexcept {
  // Covers destruction by an embedding GTK application as well as the WM path.
  shutdown(*static_cast<linux_state*>(data), true, true);
}

gboolean decide_policy(WebKitWebView*, WebKitPolicyDecision* decision,
                       WebKitPolicyDecisionType type, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  bool allow = false;
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
    auto* action = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    auto* request = webkit_navigation_action_get_request(action);
    // There is no trustworthy frame/same-document flag in this GTK API. Only
    // the first admissible action of a native load may create a document using
    // its fresh preload. A URL containing a fragment must not bypass the ticket.
    const bool selected = admissible(state, webkit_uri_request_get_uri(request));
    allow = selected && state.navigation_ticket &&
            !webkit_navigation_action_is_redirect(action);
    if (allow) {
      state.navigation_ticket = false;
    } else if (selected && !state.renderer_reload_pending &&
               webkit_navigation_action_get_navigation_type(action) == WEBKIT_NAVIGATION_TYPE_RELOAD) {
      // Cancel first, then request a new native load with a rotated token.
      // Do not treat arbitrary same-URI navigation as reload: it may be an
      // unnamed subframe, and reloading the top document would form a loop.
      webkit_policy_decision_ignore(decision);
      state.renderer_reload_pending = true;
      state.committed = false;
      ++state.generation;  // Invalidate already queued old-document callbacks.
      if (state.evaluations) g_cancellable_cancel(state.evaluations);
      try {
        const auto generation = state.generation;
        auto status = enqueue(state, [weak = state.weak_from_this(), generation] {
          auto current = weak.lock();
          if (!current || current->generation != generation) return;
          current->renderer_reload_pending = false;
          auto handler = current->events.reload;
          if (handler) handler();
        });
        if (!status.ok) report(state, std::move(status));
      } catch (...) {
        report(state, result::failure("E_RELOAD", "Could not queue a fresh document reload."));
      }
      return TRUE;
    }
  } else if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
    auto* response = WEBKIT_RESPONSE_POLICY_DECISION(decision);
    allow = state.active.load() &&
            webkit_response_policy_decision_is_mime_type_supported(response);
    if (allow && webkit_response_policy_decision_is_main_frame_main_resource(response)) {
      allow = admissible(state, webkit_uri_request_get_uri(
          webkit_response_policy_decision_get_request(response)));
    }
  }
  // NEW_WINDOW_ACTION, unsupported MIME types, and unknown policies are denied.
  if (allow) webkit_policy_decision_use(decision);
  else webkit_policy_decision_ignore(decision);
  return TRUE;
}

void script_message(WebKitUserContentManager*, WebKitJavascriptResult* message,
                    gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load() || !state.view) return;
  try {
    JSCValue* value = webkit_javascript_result_get_js_value(message);
    if (!jsc_value_is_string(value)) return;
    std::unique_ptr<GBytes, decltype(&g_bytes_unref)> bytes(jsc_value_to_string_as_bytes(value),
                                                         &g_bytes_unref);
    if (!bytes) return;
    gsize size = 0;
    const auto* payload = static_cast<const char*>(g_bytes_get_data(bytes.get(), &size));
    // Use the byte length, not strlen: embedded NULs must never turn an invalid
    // envelope into a valid prefix before common protocol validation.
    if (!payload || size > max_bridge_bytes ||
        std::memchr(payload, '\0', size) != nullptr) return;
    const char* uri = webkit_web_view_get_uri(state.view);
    if (!admissible(state, uri)) return;
    const auto generation = state.generation;
    auto status = enqueue(state, [weak = state.weak_from_this(), generation,
                                  payload = std::string(payload, size), uri = std::string(uri)] {
      auto current = weak.lock();
      if (!current || current->generation != generation || !admissible(*current, uri.c_str())) return;
      auto handler = current->events.message;
      // WebKitGTK 4.1 provides no sender frame/origin on this signal. This URI
      // describes the view's navigation context, NOT proof of the sender origin.
      // Common code authenticates the per-navigation token from the top-only
      // preload closure; never change unknown to main to bypass that validation.
      if (handler) handler(payload, uri, message_frame::unknown);
    });
    if (!status.ok) report(state, std::move(status));
  } catch (...) {
    report(state, result::failure("E_CALLBACK", "The Linux message callback failed."));
  }
}

void load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load()) return;
  if (event == WEBKIT_LOAD_STARTED) {
    state.committed = false;
    state.load_failed = false;
  } else if (event == WEBKIT_LOAD_COMMITTED) {
    state.committed = admissible(state, webkit_web_view_get_uri(view));
    if (!state.committed) webkit_web_view_stop_loading(view);
  } else if (event == WEBKIT_LOAD_FINISHED && state.committed && !state.load_failed) {
    const char* uri = webkit_web_view_get_uri(view);
    if (!admissible(state, uri)) return;
    try {
      const auto generation = state.generation;
      auto status = enqueue(state, [weak = state.weak_from_this(), generation, uri = std::string(uri)] {
        auto current = weak.lock();
        if (!current || current->generation != generation || !admissible(*current, uri.c_str())) return;
        auto handler = current->events.loaded;
        if (handler) handler(uri);
      });
      if (!status.ok) report(state, std::move(status));
    } catch (...) {
      report(state, result::failure("E_CALLBACK", "The Linux load callback failed."));
    }
  }
}

gboolean load_failed(WebKitWebView*, WebKitLoadEvent, const gchar* uri, GError* error,
                     gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  // Native navigation/close can cancel the old load. It is not a new load error.
  if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
      g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
      g_error_matches(error, WEBKIT_POLICY_ERROR,
                      WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)) return TRUE;
  if (admissible(state, uri)) {
    state.load_failed = true;
    report(state, result::failure("E_LOAD", error ? error->message : "Document loading failed."));
  }
  return TRUE;  // Do not replace the selected document with WebKit's error page.
}

void process_terminated(WebKitWebView*, WebKitWebProcessTerminationReason reason,
                        gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  state.load_failed = true;
  state.committed = false;
  const char* message = "The WebKit web process terminated. Check the installed distribution's "
    "WebKitGTK runtime, helper processes and graphics support; use the staged runtime doctor. "
    "This event does not identify a missing dependency. Do not disable the WebKit sandbox.";
  if (reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT)
    message = "The WebKit web process exceeded its memory limit. Reduce page memory use and retry.";
  else if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API)
    message = "The WebKit web process was terminated by an API request.";
  report(state, result::failure("E_WEB_PROCESS", message));
}

gboolean deny_permission(WebKitWebView*, WebKitPermissionRequest* request, gpointer) noexcept {
  webkit_permission_request_deny(request);
  return TRUE;
}

void cancel_download(WebKitWebContext*, WebKitDownload* download, gpointer) noexcept {
  webkit_download_cancel(download);
}

GtkWidget* deny_window(WebKitWebView* view, WebKitNavigationAction*, gpointer) noexcept {
  g_signal_stop_emission_by_name(view, "create");
  return nullptr;
}

gboolean deny_file_chooser(WebKitWebView*, WebKitFileChooserRequest* request, gpointer) noexcept {
  webkit_file_chooser_request_cancel(request);
  return TRUE;
}

gboolean deny_authentication(WebKitWebView*, WebKitAuthenticationRequest* request,
                             gpointer) noexcept {
  webkit_authentication_request_cancel(request);
  return TRUE;
}

gboolean deny_context_menu(WebKitWebView*, WebKitContextMenu*, GdkEvent*,
                           WebKitHitTestResult*, gpointer) noexcept {
  return TRUE;  // Avoid built-in open-window, save/download and inspect actions.
}

struct evaluation {
  std::weak_ptr<linux_state> state;
  std::uint64_t generation;
};

void evaluation_finished(GObject* object, GAsyncResult* outcome, gpointer data) noexcept {
  std::unique_ptr<evaluation> pending(static_cast<evaluation*>(data));
  GError* error = nullptr;
  JSCValue* value = webkit_web_view_evaluate_javascript_finish(
      WEBKIT_WEB_VIEW(object), outcome, &error);
  if (value) g_object_unref(value);
  if (auto state = pending->state.lock()) {
    if (state->active.load() && state->generation == pending->generation &&
        error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      report(*state, result::failure("E_JAVASCRIPT", error->message));
    }
  }
  if (error) g_error_free(error);
}

struct dispatch_task {
  std::weak_ptr<linux_state> state;
  GSource* source;
  std::function<void()> work;
};

gboolean dispatch_work(gpointer data) noexcept {
  auto& task = *static_cast<dispatch_task*>(data);
  auto state = task.state.lock();
  if (!state) return G_SOURCE_REMOVE;
  {
    std::lock_guard<std::mutex> lock(state->dispatch_mutex);
    auto& sources = state->dispatch_sources;
    auto found = std::find(sources.begin(), sources.end(), task.source);
    if (found != sources.end()) {
      sources.erase(found);
      g_source_unref(task.source);
    }
  }
  if (state->active.load() && state->owner == std::this_thread::get_id()) {
    try { task.work(); }
    catch (...) { report(*state, result::failure("E_CALLBACK", "A dispatched UI callback failed.")); }
  }
  return G_SOURCE_REMOVE;
}

result enqueue(linux_state& state, std::function<void()> work) {
  if (!work) return result::failure("E_ARGUMENT", "A UI callback is required.");
  std::lock_guard<std::mutex> lock(state.dispatch_mutex);
  if (!state.active.load()) return unavailable();
  if (state.dispatch_sources.size() >= max_pending_dispatches)
    return result::failure("E_BUSY", "The desktop dispatch queue is full.");
  // Reserve before creating GLib-owned state so allocation failure cannot leak it.
  state.dispatch_sources.reserve(state.dispatch_sources.size() + 1);
  auto task = std::make_unique<dispatch_task>(
      dispatch_task{state.weak_from_this(), nullptr, std::move(work)});
  GSource* source = g_idle_source_new();
  task->source = source;
  g_source_set_callback(source, dispatch_work, task.release(),
      [](gpointer data) { delete static_cast<dispatch_task*>(data); });
  state.dispatch_sources.push_back(source);
  g_source_attach(source, state.context);
  // Unlike g_main_context_invoke(), an attached source cannot execute on the
  // submitting worker merely because the UI loop has not acquired its context.
  return result::success();
}

struct clipboard_request {
  GMainLoop* loop;
  bool done{false};
  value_result outcome{result::failure("E_TIMEOUT", "Reading the clipboard timed out."), {}};
  explicit clipboard_request(GMainContext* context) : loop(g_main_loop_new(context, FALSE)) {}
  ~clipboard_request() { g_main_loop_unref(loop); }
};

void clipboard_received(GtkClipboard*, const gchar* text, gpointer data) noexcept {
  std::unique_ptr<std::shared_ptr<clipboard_request>> handle(
      static_cast<std::shared_ptr<clipboard_request>*>(data));
  auto& request = **handle;
  if (request.done) return;
  try {
    if (!text) {
      request.outcome = {result::failure("E_CLIPBOARD", "The clipboard contains no readable text."), {}};
    } else if (strnlen(text, max_text_bytes + 1) > max_text_bytes) {
      request.outcome = {result::failure("E_LIMIT", "Clipboard text exceeds the size limit."), {}};
    } else {
      request.outcome = {result::success(), text};
    }
  } catch (...) {}
  request.done = true;
  g_main_loop_quit(request.loop);
}

gboolean clipboard_timeout(gpointer data) noexcept {
  auto& request = *static_cast<clipboard_request*>(data);
  request.done = true;
  g_main_loop_quit(request.loop);
  return G_SOURCE_REMOVE;
}

struct modal_guard {
  linux_state& state;
  explicit modal_guard(linux_state& value) : state(value) { state.modal_busy = true; }
  ~modal_guard() { state.modal_busy = false; }
};

bool valid_menu(const std::vector<menu_item>& items, bool allow_empty) {
  if ((!allow_empty && items.empty()) || items.size() > 64) return false;
  std::set<std::string> commands;
  for (const auto& item : items) {
    if (item.separator) continue;
    if (item.id.empty() || item.id.size() > 64 ||
        item.label.empty() || !valid_text(item.label, 4096)) return false;
    for (unsigned char character : item.id)
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-')) return false;
    if (!commands.insert(item.id).second) return false;
  }
  return true;
}

struct menu_command {
  std::weak_ptr<linux_state> state;
  std::string id;
  std::uint64_t generation;
  bool tray;
};

bool current_menu(const linux_state& state, bool tray, std::uint64_t generation) noexcept {
  return state.active.load() &&
      (tray ? state.tray_menu && state.tray_generation == generation
            : state.popup && state.popup_generation == generation);
}

void menu_activated(GtkMenuItem* item, gpointer data) noexcept {
  const auto& command = *static_cast<menu_command*>(data);
  auto state = command.state.lock();
  if (!state || !gtk_widget_is_sensitive(GTK_WIDGET(item)) ||
      !current_menu(*state, command.tray, command.generation)) return;
  try {
    auto status = enqueue(*state, [weak = command.state, id = command.id,
                                  generation = command.generation, tray = command.tray] {
      auto current = weak.lock();
      if (!current || !current_menu(*current, tray, generation)) return;
      auto handler = current->events.command;
      if (handler) handler(id);
    });
    if (!status.ok) report(*state, std::move(status));
  } catch (...) {
    report(*state, result::failure("E_CALLBACK", "Could not queue the native menu command."));
  }
}

GtkWidget* make_menu(linux_state& state, const std::vector<menu_item>& items,
                     bool tray, std::uint64_t generation) {
  GtkWidget* menu = gtk_menu_new();
  g_object_ref_sink(menu);
  try {
    for (const auto& item : items) {
      GtkWidget* row = nullptr;
      if (item.separator) {
        row = gtk_separator_menu_item_new();
      } else {
        auto command = std::make_unique<menu_command>(
            menu_command{state.weak_from_this(), item.id, generation, tray});
        if (item.checked) {
          row = gtk_check_menu_item_new_with_label(item.label.c_str());
          gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(row), TRUE);
        } else {
          row = gtk_menu_item_new_with_label(item.label.c_str());
        }
        gtk_widget_set_sensitive(row, item.enabled);
        g_signal_connect_data(row, "activate", G_CALLBACK(menu_activated), command.release(),
            [](gpointer value, GClosure*) { delete static_cast<menu_command*>(value); },
            G_CONNECT_DEFAULT);
      }
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), row);
    }
    gtk_menu_attach_to_widget(GTK_MENU(menu), state.window, nullptr);
    gtk_widget_show_all(menu);
  } catch (...) {
    clear_menu(menu);
    throw;
  }
  return menu;
}

gboolean stop_tray_wait(gpointer data) noexcept {
  g_main_loop_quit(static_cast<GMainLoop*>(data));
  return G_SOURCE_REMOVE;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void tray_popup(GtkStatusIcon* icon, guint button, guint32 time, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load() || state.tray_icon != icon || !state.tray_menu ||
      !gtk_status_icon_is_embedded(icon)) return;
  gtk_menu_popup(GTK_MENU(state.tray_menu), nullptr, nullptr, gtk_status_icon_position_menu,
                 icon, button, time);
}

void tray_activate(GtkStatusIcon* icon, gpointer data) noexcept {
  tray_popup(icon, 0, gtk_get_current_event_time(), data);
}

void tray_embedding_changed(GObject* object, GParamSpec*, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  auto* icon = GTK_STATUS_ICON(object);
  if (!state.active.load() || state.tray_icon != icon) return;
  if (gtk_status_icon_is_embedded(icon)) {
    state.tray_was_embedded = true;
    if (state.tray_wait_loop) g_main_loop_quit(state.tray_wait_loop);
  } else if (state.tray_was_embedded) {
    g_object_ref(icon);
    clear_native_tray(state);
    g_object_unref(icon);
    report(state, result::failure("E_TRAY_UNAVAILABLE", "The desktop tray host disappeared."));
  }
}

result install_tray(linux_state& state, const tray_options& settings) {
  if (state.modal_busy)
    return result::failure("E_BUSY", "A modal desktop operation is already active.");
  modal_guard guard(state);
  GdkPixbuf* image = gtk_window_get_icon(GTK_WINDOW(state.window));
  if (image) {
    g_object_ref(image);
  } else {
    const char* name = gtk_window_get_icon_name(GTK_WINDOW(state.window));
    if (!name) name = gtk_window_get_default_icon_name();
    if (!name) name = "application-x-executable";
    image = gtk_icon_theme_load_icon(gtk_icon_theme_get_for_screen(
        gtk_widget_get_screen(state.window)), name, 24, GTK_ICON_LOOKUP_USE_BUILTIN, nullptr);
  }
  if (!image)
    return result::failure("E_ICON_UNAVAILABLE", "The desktop theme has no native application icon.");
  auto* icon = GTK_STATUS_ICON(g_object_new(GTK_TYPE_STATUS_ICON,
      "pixbuf", image, "visible", FALSE, nullptr));
  g_object_unref(image);
  GtkWidget* menu = nullptr;
  try {
    if (!settings.menu.empty())
      menu = make_menu(state, settings.menu, true, state.tray_generation + 1);
  } catch (...) {
    g_object_unref(icon);
    throw;
  }
  clear_native_tray(state);
  state.tray_icon = icon;
  state.tray_menu = menu;
  gtk_status_icon_set_title(icon, settings.label.c_str());
  gtk_status_icon_set_tooltip_text(icon, settings.tooltip.c_str());
  g_signal_connect(icon, "activate", G_CALLBACK(tray_activate), &state);
  g_signal_connect(icon, "popup-menu", G_CALLBACK(tray_popup), &state);
  g_signal_connect(icon, "notify::embedded", G_CALLBACK(tray_embedding_changed), &state);
  // The loop pumps UI events but is bounded. Merely creating a visible status
  // icon does NOT demonstrate that a tray host exists or displays it.
  GMainLoop* wait = g_main_loop_new(state.context, FALSE);
  GSource* timeout = g_timeout_source_new(tray_embed_timeout_ms);
  g_source_set_callback(timeout, stop_tray_wait, wait, nullptr);
  g_source_attach(timeout, state.context);
  state.tray_wait_loop = wait;
  g_object_ref(icon);  // A nested UI event can close this window and clear its tray.
  gtk_status_icon_set_visible(icon, TRUE);
  if (!gtk_status_icon_is_embedded(icon) && state.active.load())
    g_main_loop_run(wait);
  state.tray_wait_loop = nullptr;
  g_source_destroy(timeout);
  g_source_unref(timeout);
  g_main_loop_unref(wait);
  const bool current = state.tray_icon == icon;
  const bool embedded = current && gtk_status_icon_is_embedded(icon);
  g_object_unref(icon);
  if (!state.active.load()) return unavailable();
  if (!current)
    return result::failure("E_CANCELLED", "Tray installation was cancelled.");
  if (!embedded) {
    clear_native_tray(state);
    return result::failure("E_TRAY_UNAVAILABLE",
        "No compatible XEmbed tray host embedded the application icon within 1500 ms.");
  }
  state.tray_was_embedded = true;
  return result::success();
}
G_GNUC_END_IGNORE_DEPRECATIONS

class linux_backend final : public backend {
 public:
  linux_backend() : state_(std::make_shared<linux_state>()) {}
  ~linux_backend() override { shutdown(*state_, false); }

  result create(const options& settings, backend_events events) override {
    auto& state = *state_;
    if (state.owner != std::this_thread::get_id())
      return result::failure("E_THREAD", "GTK must be initialized on the desktop UI thread.");
    if (state.initialized || state.created)
      return result::failure("E_STATE", "A desktop backend can only be initialized once.");
    if (!valid_text(settings.title) || settings.width < 1 || settings.height < 1)
      return result::failure("E_ARGUMENT", "Invalid window title or dimensions.");
    const auto runtime = check_webkit_helpers(SOUI_DESKTOP_WEBKIT_HELPER_DIR);
    if (!runtime.ok) return runtime;
    if (!gtk_init_check(nullptr, nullptr))
      return result::failure("E_DISPLAY", "GTK could not connect to an X11 or Wayland display. Start the application in an "
        "active graphical user session and verify display access; setting a display variable "
        "alone does not create a display server. No security settings were changed.");

    if (settings.user_data_directory.empty()) {
      state.data = webkit_website_data_manager_new_ephemeral();
    } else {
      std::error_code error;
      auto root = std::filesystem::absolute(settings.user_data_directory, error);
      if (error) return result::failure("E_PROFILE", error.message());
      std::filesystem::create_directories(root, error);
      if (error) return result::failure("E_PROFILE", error.message());
      const auto data_path = (root / "data").string();
      const auto cache_path = (root / "cache").string();
      state.data = webkit_website_data_manager_new(
          "base-data-directory", data_path.c_str(), "base-cache-directory", cache_path.c_str(), nullptr);
    }
    state.created = true;
    state.web_context = webkit_web_context_new_with_website_data_manager(state.data);
    state.content = webkit_user_content_manager_new();
    state.evaluations = g_cancellable_new();
    // Connect before registration, as required by WebKit's message-handler API.
    g_signal_connect(state.content, "script-message-received::soui", G_CALLBACK(script_message), &state);
    if (!webkit_user_content_manager_register_script_message_handler(state.content, "soui")) {
      shutdown(state, false);
      return result::failure("E_BRIDGE", "Could not register the WebKit message handler.");
    }
    state.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", state.web_context, "user-content-manager", state.content, nullptr));
    g_object_ref_sink(state.view);
    auto* preferences = webkit_web_view_get_settings(state.view);
    webkit_settings_set_javascript_can_open_windows_automatically(preferences, FALSE);
    webkit_settings_set_javascript_can_access_clipboard(preferences, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(preferences, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(preferences, FALSE);
    webkit_settings_set_enable_developer_extras(preferences, FALSE);
    webkit_settings_set_enable_page_cache(preferences, FALSE);
    webkit_settings_set_allow_modal_dialogs(preferences, FALSE);
    // Preserve WebKit's web security and process sandbox defaults.
    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_ref_sink(state.window);
    gtk_window_set_title(GTK_WINDOW(state.window), settings.title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(state.window), settings.width, settings.height);
    gtk_container_add(GTK_CONTAINER(state.window), GTK_WIDGET(state.view));
    g_signal_connect(state.window, "delete-event", G_CALLBACK(window_delete), &state);
    g_signal_connect(state.window, "destroy", G_CALLBACK(window_destroy), &state);
    g_signal_connect(state.view, "decide-policy", G_CALLBACK(decide_policy), &state);
    g_signal_connect(state.view, "load-changed", G_CALLBACK(load_changed), &state);
    g_signal_connect(state.view, "load-failed", G_CALLBACK(load_failed), &state);
    g_signal_connect(state.view, "web-process-terminated", G_CALLBACK(process_terminated), &state);
    g_signal_connect(state.view, "permission-request", G_CALLBACK(deny_permission), &state);
    g_signal_connect(state.view, "create", G_CALLBACK(deny_window), &state);
    g_signal_connect(state.view, "run-file-chooser", G_CALLBACK(deny_file_chooser), &state);
    g_signal_connect(state.view, "authenticate", G_CALLBACK(deny_authentication), &state);
    g_signal_connect(state.view, "context-menu", G_CALLBACK(deny_context_menu), &state);
    g_signal_connect(state.web_context, "download-started", G_CALLBACK(cancel_download), &state);
    state.events = std::move(events);
    state.enable_resource_server = settings.enable_resource_server;
    state.initialized = true;
    state.active.store(true);
    if (settings.visible) gtk_widget_show_all(state.window);
    else gtk_widget_show(GTK_WIDGET(state.view));
    return result::success();
  }

  result load(std::string uri, std::string preload) override {
    auto& state = *state_;
    auto status = check_ui(state);
    if (!status.ok) return status;
    if (!valid_text(uri) || !valid_text(preload) || !admissible(state, uri.c_str()))
      return result::failure("E_NAVIGATION", "The document or preload is not admissible.");
    ++state.generation;
    state.navigation_ticket = false;
    state.renderer_reload_pending = false;
    state.committed = false;
    state.load_failed = false;
    g_cancellable_cancel(state.evaluations);
    g_clear_object(&state.evaluations);
    state.evaluations = g_cancellable_new();
    webkit_web_view_stop_loading(state.view);
    webkit_user_content_manager_remove_all_scripts(state.content);
    auto* script = webkit_user_script_new(preload.c_str(),
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(state.content, script);
    webkit_user_script_unref(script);
    state.navigation_ticket = true;
    webkit_web_view_load_uri(state.view, uri.c_str());
    return result::success();
  }

  result run() override {
    if (state_->owner != std::this_thread::get_id())
      return result::failure("E_THREAD", "Running the desktop loop requires the UI thread.");
    if (!state_->initialized)
      return result::failure("E_STATE", "The desktop backend has not been initialized.");
    if (state_->running) return result::failure("E_STATE", "The desktop event loop is already running.");
    if (state_->loop_quit_requested) return result::success();
    // The root window may already be closed while its children still run.
    state_->running = true;
    g_main_loop_run(state_->loop);
    state_->running = false;
    return result::success();
  }

  result quit_loop() override {
    if (state_->owner != std::this_thread::get_id())
      return result::failure("E_THREAD", "Stopping the desktop loop requires the UI thread.");
    state_->loop_quit_requested = true;
    g_main_loop_quit(state_->loop);
    return result::success();
  }

  result close() override {
    if (state_->owner != std::this_thread::get_id())
      return result::failure("E_THREAD", "Closing the desktop requires the UI thread.");
    shutdown(*state_, true);
    return result::success();
  }

  result evaluate(std::string script) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (!valid_text(script)) return result::failure("E_ARGUMENT", "Invalid JavaScript source.");
    auto* pending = new evaluation{state_, state_->generation};
    webkit_web_view_evaluate_javascript(state_->view, script.data(),
        static_cast<gssize>(script.size()), nullptr, nullptr, state_->evaluations,
        evaluation_finished, pending);
    return result::success();
  }

  result dispatch(std::function<void()> work) override {
    return enqueue(*state_, std::move(work));
  }

  result set_title(std::string_view title) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (!valid_text(title)) return result::failure("E_ARGUMENT", "Invalid window title.");
    gtk_window_set_title(GTK_WINDOW(state_->window), std::string(title).c_str());
    return result::success();
  }

  result resize(int width, int height) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (width < 1 || height < 1 || width > 32768 || height > 32768)
      return result::failure("E_ARGUMENT", "Window dimensions must be between 1 and 32768.");
    gtk_window_resize(GTK_WINDOW(state_->window), width, height);
    return result::success();
  }

  result show(bool visible) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (visible) gtk_widget_show_all(state_->window);
    else gtk_widget_hide(state_->window);
    return result::success();
  }

  result focus() override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    gtk_window_present(GTK_WINDOW(state_->window));
    gtk_widget_grab_focus(GTK_WIDGET(state_->view));
    return result::success();  // Window-manager focus-stealing policy still applies.
  }

  value_result clipboard_read() override {
    auto status = check_ui(*state_);
    if (!status.ok) return {status, {}};
    if (state_->modal_busy)
      return {result::failure("E_BUSY", "A modal desktop operation is already active."), {}};
    modal_guard guard(*state_);
    auto request = std::make_shared<clipboard_request>(state_->context);
    GSource* timeout = g_timeout_source_new(clipboard_timeout_ms);
    g_source_set_callback(timeout, clipboard_timeout, request.get(), nullptr);
    g_source_attach(timeout, state_->context);
    state_->clipboard_loop = request->loop;
    gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), clipboard_received,
        new std::shared_ptr<clipboard_request>(request));
    if (!request->done) g_main_loop_run(request->loop);
    state_->clipboard_loop = nullptr;
    request->done = true;
    g_source_destroy(timeout);
    g_source_unref(timeout);
    if (!state_->active.load()) return {unavailable(), {}};
    return std::move(request->outcome);
  }

  result clipboard_write(std::string_view text) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (!valid_text(text)) return result::failure("E_ARGUMENT", "Invalid or oversized clipboard text.");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.empty() ? "" : text.data(),
                          static_cast<gint>(text.size()));
    return result::success();
  }

  value_result choose_directory() override {
    auto status = check_ui(*state_);
    if (!status.ok) return {status, {}};
    if (state_->modal_busy)
      return {result::failure("E_BUSY", "A modal desktop operation is already active."), {}};
    modal_guard guard(*state_);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Choose directory", GTK_WINDOW(state_->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, nullptr);
    g_object_ref_sink(dialog);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), FALSE);
    state_->directory_dialog = dialog;
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    state_->directory_dialog = nullptr;
    value_result outcome{result::failure("E_CANCELLED", "Directory selection was cancelled."), {}};
    if (!state_->active.load()) {
      outcome.status = unavailable();
    } else if (response == GTK_RESPONSE_ACCEPT) {
      gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      if (filename) {
        gchar* utf8 = g_filename_to_utf8(filename, -1, nullptr, nullptr, nullptr);
        if (utf8) outcome = {result::success(), utf8};
        else outcome.status = result::failure("E_ENCODING", "The selected path is not valid UTF-8.");
        g_free(utf8);
        g_free(filename);
      } else {
        outcome.status = result::failure("E_DIALOG", "No local directory was selected.");
      }
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    return outcome;
  }

  result popup_menu(const std::vector<menu_item>& items) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (!valid_menu(items, false))
      return result::failure("E_ARGUMENT", "A popup menu needs at most 64 valid, unique commands.");
    if (!gtk_widget_get_mapped(state_->window))
      return result::failure("E_WINDOW_HIDDEN", "Show the owning window before opening its popup menu.");
    GtkWidget* menu = make_menu(*state_, items, false, state_->popup_generation + 1);
    ++state_->popup_generation;
    clear_menu(state_->popup);
    state_->popup = menu;
    GdkEvent* event = gtk_get_current_event();
    if (!event) {
      event = gdk_event_new(GDK_BUTTON_PRESS);
      event->button.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(state_->window)));
      event->button.send_event = TRUE;
      event->button.time = GDK_CURRENT_TIME;
      event->button.button = 3;
      auto* seat = gdk_display_get_default_seat(gtk_widget_get_display(state_->window));
      if (seat) gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    }
    gtk_menu_popup_at_widget(GTK_MENU(menu), state_->window, GDK_GRAVITY_CENTER,
                             GDK_GRAVITY_NORTH_WEST, event);
    gdk_event_free(event);
    return result::success();
  }

  result set_tray(const tray_options& settings) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (settings.label.empty() || !valid_text(settings.label, 256) ||
        !valid_text(settings.tooltip, 4096) || !valid_menu(settings.menu, true))
      return result::failure("E_ARGUMENT", "Invalid native tray label, tooltip, or menu.");
    return install_tray(*state_, settings);
  }

  result clear_tray() override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    clear_native_tray(*state_);
    return result::success();
  }

  capabilities get_capabilities() const override {
    return {"webkitgtk", true, true, true, true, true, true, true};
  }

  bool has_message_frame_metadata() const override { return false; }

 private:
  std::shared_ptr<linux_state> state_;
};

}  // namespace

std::unique_ptr<backend> make_backend() {
  return std::make_unique<linux_backend>();
}

}  // namespace soui::desktop::detail
