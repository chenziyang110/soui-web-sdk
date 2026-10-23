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
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
#include <gdk/gdkx.h>
#include <X11/keysym.h>
#endif

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
constexpr std::size_t max_selected_files = 32;
constexpr std::size_t max_selected_path_bytes = 32768;
constexpr std::size_t max_selected_total_bytes = 128 * 1024;

#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
struct x11_shortcut;
#endif

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
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
  std::vector<std::shared_ptr<x11_shortcut>> shortcuts;
#endif

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

result check_evaluation(const linux_state& state) {
  auto status = check_ui(state);
  if (!status.ok) return status;
  if (state.load_failed)
    return result::failure("E_LOAD", "The document failed. Navigate or reload before evaluating JavaScript.");
  if (state.renderer_reload_pending ||
      (state.evaluations && g_cancellable_is_cancelled(state.evaluations)))
    return result::failure("E_NAVIGATION", "A fresh native document load is required before evaluating JavaScript.");
  // Do not require LOAD_COMMITTED or the public ready callback: the normal
  // document-start Bridge can need a native reply before loading finishes.
  return result::success();
}

bool valid_text(std::string_view text, std::size_t limit = max_text_bytes) {
  if (text.empty()) return true;
  return text.size() <= limit && text.find('\0') == std::string_view::npos &&
         g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr);
}

result enqueue(linux_state& state, std::function<void()> work);

// Native/window work can survive a failed document. Document callbacks cannot:
// retain the generation at submission and reject a failure before dispatch.
result enqueue_document(linux_state& state, std::function<void(linux_state&)> work) {
  const auto generation = state.generation;
  return enqueue(state, [weak = state.weak_from_this(), generation, work = std::move(work)] {
    auto current = weak.lock();
    if (!current || current->generation != generation || current->load_failed) return;
    work(*current);
  });
}

void fail_document(linux_state& state) noexcept {
  state.load_failed = true;
  state.committed = false;
  state.navigation_ticket = false;
  state.renderer_reload_pending = false;
  ++state.generation;
  if (state.evaluations) g_cancellable_cancel(state.evaluations);
}

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

#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
GdkFilterReturn shortcut_filter(GdkXEvent*, GdkEvent*, gpointer) noexcept;

// XGrabKey carries no registration ID. Never reuse a connection for another
// registration: even an already queued native event belongs only to this token.
// GDK owns event processing; no XNextEvent/draining of GTK's connection is used.
struct x11_shortcut : std::enable_shared_from_this<x11_shortcut> {
  std::weak_ptr<linux_state> state;
  std::uint64_t token{0};
  GdkDisplay* display{nullptr};  // An extra owned reference, never GTK's display.
  GdkKeymap* keymap{nullptr};
  std::vector<Window> roots;
  std::vector<unsigned int> variants;
  unsigned int keycode{0};
  unsigned int modifiers{0};
  unsigned int lock_mask{0};
  bool enabled{false};
  bool filter_installed{false};
  bool callback_pending{false};

  void close_connection() noexcept {
    enabled = false;
    if (std::exchange(filter_installed, false))
      gdk_window_remove_filter(nullptr, shortcut_filter, this);
    if (keymap) {
      g_signal_handlers_disconnect_by_data(keymap, this);
      g_clear_object(&keymap);
    }
    if (auto* owned = std::exchange(display, nullptr)) {
      g_signal_handlers_disconnect_by_data(owned, this);
      // Closing our connection releases even partially acquired/failed grabs.
      if (!gdk_display_is_closed(owned)) gdk_display_close(owned);
      g_object_unref(owned);
    }
  }

  ~x11_shortcut() { close_connection(); }
};

bool x11_shortcuts_available(const linux_state& state) noexcept {
  const auto* session = g_getenv("XDG_SESSION_TYPE");
  // XWayland is not a desktop-global implementation. An unknown session also
  // fails closed instead of guessing from DISPLAY or GDK's backend alone.
  if (g_getenv("WAYLAND_DISPLAY") || !session || g_ascii_strcasecmp(session, "x11") != 0)
    return false;
  auto* display = state.window ? gtk_widget_get_display(state.window) : gdk_display_get_default();
  return display && !gdk_display_is_closed(display) && GDK_IS_X11_DISPLAY(display);
}

guint shortcut_keyval(shortcut_key key) noexcept {
  switch (key) {
    case shortcut_key::f1: return XK_F1;
    case shortcut_key::f2: return XK_F2;
    case shortcut_key::f3: return XK_F3;
    case shortcut_key::f4: return XK_F4;
    case shortcut_key::f5: return XK_F5;
    case shortcut_key::f6: return XK_F6;
    case shortcut_key::f7: return XK_F7;
    case shortcut_key::f8: return XK_F8;
    case shortcut_key::f9: return XK_F9;
    case shortcut_key::f10: return XK_F10;
    case shortcut_key::f11: return XK_F11;
    case shortcut_key::left: return XK_Left;
    case shortcut_key::right: return XK_Right;
    case shortcut_key::up: return XK_Up;
    case shortcut_key::down: return XK_Down;
    case shortcut_key::home: return XK_Home;
    case shortcut_key::end: return XK_End;
    case shortcut_key::page_up: return XK_Page_Up;
    case shortcut_key::page_down: return XK_Page_Down;
    case shortcut_key::insert_key: return XK_Insert;
    case shortcut_key::delete_key: return XK_Delete;
  }
  return 0;
}

int ungrab_shortcut(x11_shortcut& shortcut) noexcept {
  shortcut.enabled = false;  // Disable queued callbacks before any native work.
  if (!shortcut.display || gdk_display_is_closed(shortcut.display)) return 0;
  auto* display = gdk_x11_display_get_xdisplay(shortcut.display);
  gdk_x11_display_error_trap_push(shortcut.display);
  for (const auto root : shortcut.roots)
    for (const auto modifiers : shortcut.variants)
      XUngrabKey(display, static_cast<int>(shortcut.keycode), modifiers, root);
  // pop synchronizes outstanding requests on this display and reports errors.
  return gdk_x11_display_error_trap_pop(shortcut.display);
}

void invalidate_shortcut(x11_shortcut& shortcut, bool mapping_changed) noexcept {
  if (!shortcut.enabled) return;
  const auto cleanup_error = ungrab_shortcut(shortcut);
  try {
    auto state = shortcut.state.lock();
    if (!state || !state->active.load()) return;
    // Keep the inactive native record until explicit unregister/close. A failed
    // ungrab can be retried, and errors never reactivate the registration.
    (void)enqueue(*state, [weak = shortcut.weak_from_this(), mapping_changed, cleanup_error] {
      auto registration = weak.lock();
      if (!registration || !registration->display) return;
      auto current = registration->state.lock();
      if (!current || !current->active.load()) return;
      report(*current, result::failure(cleanup_error ? "E_SHORTCUT_UNREGISTER" : "E_SHORTCUT_INVALIDATED", cleanup_error
          ? "The X11 shortcut was disabled, but native cleanup failed. Unregister it again or close the window."
          : mapping_changed
              ? "The X11 keyboard mapping changed. Unregister and register the global shortcut again."
              : "The X11 shortcut connection closed. Unregister and register the global shortcut again."));
    });
  } catch (...) {
    // The registration remains disabled even if no error callback can be queued.
  }
}

void shortcut_mapping_changed(GdkKeymap*, gpointer data) noexcept {
  invalidate_shortcut(*static_cast<x11_shortcut*>(data), true);
}

void shortcut_display_closed(GdkDisplay*, gboolean, gpointer data) noexcept {
  invalidate_shortcut(*static_cast<x11_shortcut*>(data), false);
}

GdkFilterReturn shortcut_filter(GdkXEvent* native, GdkEvent*, gpointer data) noexcept {
  auto& shortcut = *static_cast<x11_shortcut*>(data);
  auto& event = *static_cast<XEvent*>(native);
  if (!shortcut.display || event.xany.display != gdk_x11_display_get_xdisplay(shortcut.display))
    return GDK_FILTER_CONTINUE;
  if (event.type == MappingNotify &&
      (event.xmapping.request == MappingKeyboard || event.xmapping.request == MappingModifier)) {
    invalidate_shortcut(shortcut, true);
    return GDK_FILTER_CONTINUE;
  }
  if ((event.type != KeyPress && event.type != KeyRelease) || event.xkey.keycode != shortcut.keycode)
    return GDK_FILTER_CONTINUE;
  constexpr unsigned int modifier_mask = ShiftMask | LockMask | ControlMask |
      Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask;
  if (event.type != KeyPress || event.xkey.send_event || !shortcut.enabled || shortcut.callback_pending ||
      (event.xkey.state & modifier_mask & ~shortcut.lock_mask) != shortcut.modifiers)
    return GDK_FILTER_REMOVE;
  try {
    auto state = shortcut.state.lock();
    if (!state || !state->active.load()) return GDK_FILTER_REMOVE;
    // Coalesce repeat events while one callback is pending: at most one queued
    // event per registration, in addition to the common bounded dispatch queue.
    shortcut.callback_pending = true;
    const auto status = enqueue(*state, [weak = shortcut.weak_from_this()] {
      auto registration = weak.lock();
      if (!registration) return;
      registration->callback_pending = false;
      auto current = registration->state.lock();
      if (!current || !current->active.load() || !registration->enabled) return;
      auto handler = current->events.global_shortcut;
      if (handler) handler(registration->token);
    });
    if (!status.ok) shortcut.callback_pending = false;
  } catch (...) {
    shortcut.callback_pending = false;
  }
  return GDK_FILTER_REMOVE;
}

bool stable_shortcut_key(x11_shortcut& shortcut, guint keyval) {
  auto* display = gdk_x11_display_get_xdisplay(shortcut.display);
  shortcut.keycode = XKeysymToKeycode(display, keyval);
  if (!shortcut.keycode) return false;
  GdkKeymapKey* keys = nullptr;
  guint* values = nullptr;
  gint count = 0;
  const bool found = gdk_keymap_get_entries_for_keycode(
      shortcut.keymap, shortcut.keycode, &keys, &values, &count);
  bool base_found = false;
  bool stable = found;
  for (gint i = 0; i < count; ++i) {
    if (keys[i].level != 0) continue;
    base_found = true;
    // Do not silently interpret one physical key as a different navigation key
    // when the active layout group changes without a mapping-change event.
    if (values[i] != keyval) stable = false;
  }
  g_free(keys);
  g_free(values);
  return stable && base_found;
}

bool shortcut_modifier_masks(x11_shortcut& shortcut, std::uint32_t requested) {
  auto* display = gdk_x11_display_get_xdisplay(shortcut.display);
  auto* mapping = XGetModifierMapping(display);
  if (!mapping) return false;
  int first_code = 0;
  int last_code = 0;
  XDisplayKeycodes(display, &first_code, &last_code);
  int symbols_per_code = 0;
  auto* symbols = XGetKeyboardMapping(display, static_cast<KeyCode>(first_code),
      last_code - first_code + 1, &symbols_per_code);
  if (!symbols || symbols_per_code < 1) {
    if (symbols) XFree(symbols);
    XFreeModifiermap(mapping);
    return false;
  }
  unsigned int alt_mask = 0;
  unsigned int lock_mask = 0;
  bool ambiguous_lock = false;
  for (int modifier = 0; modifier < 8; ++modifier) {
    bool has_lock = false;
    bool has_other = false;
    for (int slot = 0; slot < mapping->max_keypermod; ++slot) {
      const auto code = mapping->modifiermap[modifier * mapping->max_keypermod + slot];
      if (!code) continue;
      const auto symbol = code >= first_code && code <= last_code
          ? symbols[(code - first_code) * symbols_per_code] : NoSymbol;
      if (symbol == XK_Alt_L || symbol == XK_Alt_R) alt_mask |= 1u << modifier;
      const bool lock = symbol == XK_Caps_Lock || symbol == XK_Shift_Lock ||
          symbol == XK_Num_Lock || symbol == XK_Scroll_Lock;
      has_lock = has_lock || lock;
      has_other = has_other || !lock;
    }
    if (has_lock) lock_mask |= 1u << modifier;
    ambiguous_lock = ambiguous_lock || (has_lock && has_other);
  }
  XFree(symbols);
  XFreeModifiermap(mapping);
  // Mixed-purpose lock masks cannot safely be ignored. Alt must name one real
  // modifier, not a guess that Mod1 is Alt on every user's keyboard mapping.
  if (ambiguous_lock || (lock_mask & (ControlMask | ShiftMask)) ||
      ((requested & shortcut_modifiers::alt) && (!alt_mask || (alt_mask & (alt_mask - 1)) ||
        (alt_mask & (lock_mask | ControlMask | ShiftMask))))) return false;
  if (requested & (shortcut_modifiers::control | shortcut_modifiers::primary))
    shortcut.modifiers |= ControlMask;
  if (requested & shortcut_modifiers::alt) shortcut.modifiers |= alt_mask;
  if (requested & shortcut_modifiers::shift) shortcut.modifiers |= ShiftMask;
  shortcut.lock_mask = lock_mask;
  for (unsigned int variant = 0; variant < 256; ++variant)
    if ((variant & ~lock_mask) == 0) shortcut.variants.push_back(shortcut.modifiers | variant);
  return true;
}

result install_shortcut(linux_state& state, std::uint64_t token, const global_shortcut& chord) {
  if (!x11_shortcuts_available(state))
    return result::failure("not_supported", "Global shortcuts require a confirmed X11 session (XDG_SESSION_TYPE=x11), not Wayland or XWayland.");
  constexpr auto all_modifiers = shortcut_modifiers::control | shortcut_modifiers::alt |
      shortcut_modifiers::shift | shortcut_modifiers::primary;
  const auto keyval = shortcut_keyval(chord.key);
  if (!token || !keyval || (chord.modifiers & ~all_modifiers) ||
      !(chord.modifiers & (shortcut_modifiers::control | shortcut_modifiers::alt | shortcut_modifiers::primary)))
    return result::failure("E_ARGUMENT", "Invalid global shortcut key, modifiers, or registration token.");
  if (state.shortcuts.size() >= 32)
    return result::failure("E_SHORTCUT_LIMIT", "A window can own at most 32 global shortcut registrations.");
  for (const auto& existing : state.shortcuts)
    if (existing->token == token)
      return result::failure("E_SHORTCUT_DUPLICATE", "The global shortcut token is already registered.");
  state.shortcuts.reserve(state.shortcuts.size() + 1);
  auto registration = std::make_shared<x11_shortcut>();
  registration->state = state.weak_from_this();
  registration->token = token;
  auto* main_display = gtk_widget_get_display(state.window);
  auto* display = gdk_display_open(gdk_display_get_name(main_display));
  if (!display)
    return result::failure("E_DISPLAY", "Could not open a dedicated X11 shortcut connection.");
  // gdk_display_open is transfer-none; hold a reference across display close.
  // Never close/reconfigure GTK's main display, even on an unexpected result.
  if (display == main_display)
    return result::failure("E_DISPLAY", "The shortcut connection was not independent of GTK.");
  registration->display = GDK_DISPLAY(g_object_ref(display));
  if (!GDK_IS_X11_DISPLAY(display))
    return result::failure("not_supported", "The shortcut connection is not X11.");
  registration->keymap = GDK_KEYMAP(g_object_ref(gdk_keymap_get_for_display(display)));
  if (!stable_shortcut_key(*registration, keyval) || !shortcut_modifier_masks(*registration, chord.modifiers))
    return result::failure("not_supported", "The X11 keyboard mapping cannot represent this shortcut unambiguously.");
  auto* native = gdk_x11_display_get_xdisplay(display);
  for (int screen = 0; screen < ScreenCount(native); ++screen)
    registration->roots.push_back(RootWindow(native, screen));
  g_signal_connect(registration->keymap, "keys-changed", G_CALLBACK(shortcut_mapping_changed), registration.get());
  g_signal_connect(display, "closed", G_CALLBACK(shortcut_display_closed), registration.get());
  // A global GDK filter sees MappingNotify (which has no window), but inspects
  // and consumes key events ONLY from this registration's dedicated connection.
  gdk_window_add_filter(nullptr, shortcut_filter, registration.get());
  registration->filter_installed = true;
  gdk_x11_display_error_trap_push(display);
  for (const auto root : registration->roots)
    for (const auto modifiers : registration->variants)
      XGrabKey(native, static_cast<int>(registration->keycode), modifiers, root,
               False, GrabModeAsync, GrabModeAsync);
  const auto error = gdk_x11_display_error_trap_pop(display);
  if (error)
    return result::failure(error == BadAccess ? "E_SHORTCUT_CONFLICT" : "E_SHORTCUT_REGISTER",
        error == BadAccess ? "The global shortcut is already grabbed or reserved on X11."
                           : "X11 rejected the global shortcut registration.");
  // Opening a display can synchronously notify application signal handlers.
  // If one closed the owner, let this unpublished record release every grab.
  if (!state.active.load() || !state.created || !state.window) return unavailable();
  registration->enabled = true;
  state.shortcuts.push_back(std::move(registration));
  return result::success();
}
#endif

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
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
  // Closing each private connection also releases failed/partial ungrabs before
  // destroying the owning GTK window. All queued callbacks use weak records.
  for (const auto& shortcut : state.shortcuts) shortcut->enabled = false;
  for (const auto& shortcut : state.shortcuts) shortcut->close_connection();
  state.shortcuts.clear();
#endif

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

gboolean window_delete(GtkWidget* window, GdkEvent*, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load() || state.window != window) return TRUE;
  try {
    // Approval belongs to the common layer. It may close the window or destroy
    // its application synchronously, so retain state and copy the callback.
    auto keep_alive = state.shared_from_this();
    auto handler = state.events.close_requested;
    if (handler) handler();
  } catch (...) {
    // A missing or failed request must not bypass approval, and no C++
    // exception may cross the GTK signal boundary.
  }
  // Never let GTK destroy the window automatically. An approved request uses
  // backend close(), which destroys it directly without another delete-event.
  return TRUE;
}

void window_destroy(GtkWidget*, gpointer data) noexcept {
  // Covers destruction by an embedding GTK application as well as the WM path.
  shutdown(*static_cast<linux_state*>(data), true, true);
}

window_state observed_window_state(GdkWindowState native) noexcept {
  // An iconified window may retain its maximized bit. Do not make the flags
  // mutually exclusive or infer either one from the last requested operation.
  return {(native & GDK_WINDOW_STATE_ICONIFIED) != 0,
          (native & GDK_WINDOW_STATE_MAXIMIZED) != 0};
}

gboolean window_state_changed(GtkWidget* window, GdkEventWindowState* event,
                              gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load() || state.window != window || !event ||
      !(event->changed_mask & (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED)))
    return FALSE;
  try {
    // The handler can close or destroy its application. Keep the native state
    // alive across that callback, and never access the window afterward.
    auto keep_alive = state.shared_from_this();
    auto handler = state.events.state_changed;
    if (handler) handler(observed_window_state(event->new_window_state));
  } catch (...) {
    // No C++ exception may cross the GTK signal boundary.
  }
  return FALSE;
}

gboolean decide_policy(WebKitWebView*, WebKitPolicyDecision* decision,
                       WebKitPolicyDecisionType type, gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load() || state.load_failed) {
    webkit_policy_decision_ignore(decision);
    return TRUE;
  }
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
        auto status = enqueue_document(state, [](linux_state& current) {
          current.renderer_reload_pending = false;
          auto handler = current.events.reload;
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
  if (!state.active.load() || state.load_failed || !state.view) return;
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
    auto status = enqueue_document(state, [payload = std::string(payload, size),
                                           uri = std::string(uri)](linux_state& current) {
      if (!admissible(current, uri.c_str())) return;
      auto handler = current.events.message;
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
  if (!state.active.load() || state.load_failed) return;
  if (event == WEBKIT_LOAD_STARTED) {
    state.committed = false;
    // Only native load() clears a failed document, never a late engine signal.
  } else if (event == WEBKIT_LOAD_COMMITTED) {
    state.committed = admissible(state, webkit_web_view_get_uri(view));
    if (!state.committed) webkit_web_view_stop_loading(view);
  } else if (event == WEBKIT_LOAD_FINISHED && state.committed && !state.load_failed) {
    const char* uri = webkit_web_view_get_uri(view);
    if (!admissible(state, uri)) return;
    try {
      auto status = enqueue_document(state, [uri = std::string(uri)](linux_state& current) {
        if (!admissible(current, uri.c_str())) return;
        auto handler = current.events.loaded;
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
    fail_document(state);
    report(state, result::failure("E_LOAD", error ? error->message : "Document loading failed."));
  }
  return TRUE;  // Do not replace the selected document with WebKit's error page.
}

void process_terminated(WebKitWebView*, WebKitWebProcessTerminationReason reason,
                        gpointer data) noexcept {
  auto& state = *static_cast<linux_state*>(data);
  if (!state.active.load()) return;
  fail_document(state);
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

bool report_evaluation_error(const GError* error) {
  // We do not expose JS completion values. WebKit reports INVALID_RESULT when
  // execution has no exception but its value (e.g. postMessage's Promise) cannot
  // be serialized. Real script exceptions use SCRIPT_FAILED and must surface.
  // Match both domain and code, never the human-readable error text.
  return error &&
      !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
      !g_error_matches(error, WEBKIT_JAVASCRIPT_ERROR, WEBKIT_JAVASCRIPT_ERROR_INVALID_RESULT);
}

void evaluation_finished(GObject* object, GAsyncResult* outcome, gpointer data) noexcept {
  std::unique_ptr<evaluation> pending(static_cast<evaluation*>(data));
  GError* error = nullptr;
  JSCValue* value = webkit_web_view_evaluate_javascript_finish(
      WEBKIT_WEB_VIEW(object), outcome, &error);
  if (value) g_object_unref(value);
  if (auto state = pending->state.lock()) {
    if (state->active.load() && state->generation == pending->generation &&
        report_evaluation_error(error)) {
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

struct file_dialog_guard {
  linux_state& state;
  GtkWidget* dialog;
  file_dialog_guard(linux_state& owner, GtkWidget* widget) : state(owner), dialog(widget) {
    g_object_ref_sink(dialog);
    state.directory_dialog = dialog;
  }
  ~file_dialog_guard() {
    if (state.directory_dialog == dialog) state.directory_dialog = nullptr;
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
  }
};

struct selected_filenames {
  GSList* values;
  ~selected_filenames() { g_slist_free_full(values, g_free); }
};

// Keep the owner alive while GTK dispatches arbitrary application callbacks in
// its modal loop. The same slot lets shutdown destroy any active file chooser.
file_selection_result choose_file_paths(std::shared_ptr<linux_state> state,
    GtkFileChooserAction action, const std::string& title,
    const std::vector<std::string>& extensions, bool multiple,
    const std::string& default_name = {}) {
  auto status = check_ui(*state);
  if (!status.ok) return {status, {}};
  if (state->modal_busy)
    return {result::failure("E_BUSY", "A modal desktop operation is already active."), {}};
  modal_guard modal(*state);
  const bool saving = action == GTK_FILE_CHOOSER_ACTION_SAVE;
  GtkWidget* dialog = gtk_file_chooser_dialog_new(title.empty() ? nullptr : title.c_str(),
      GTK_WINDOW(state->window), action, "_Cancel", GTK_RESPONSE_CANCEL,
      saving ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
  if (!dialog)
    return {result::failure("E_DIALOG", "The native file chooser could not be created."), {}};
  file_dialog_guard dialog_guard(*state, dialog);
  auto* chooser = GTK_FILE_CHOOSER(dialog);
  gtk_file_chooser_set_local_only(chooser, TRUE);
  gtk_file_chooser_set_select_multiple(chooser, multiple ? TRUE : FALSE);
  gtk_file_chooser_set_create_folders(chooser, FALSE);
  if (saving) {
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    if (!default_name.empty()) gtk_file_chooser_set_current_name(chooser, default_name.c_str());
  }
  if (!extensions.empty()) {
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Supported files");
    for (const auto& extension : extensions) {
      const auto pattern = "*." + extension;
      gtk_file_filter_add_pattern(filter, pattern.c_str());
    }
    gtk_file_chooser_add_filter(chooser, filter);
    gtk_file_chooser_set_filter(chooser, filter);
  }

  const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (!state->active.load()) return {unavailable(), {}};
  if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT)
    return {result::failure("E_CANCELLED", "File selection was cancelled."), {}};
  if (response != GTK_RESPONSE_ACCEPT)
    return {result::failure("E_DIALOG", "The native file chooser did not complete."), {}};

  selected_filenames filenames{gtk_file_chooser_get_filenames(chooser)};
  if (!filenames.values)
    return {result::failure("E_DIALOG", "No local file was selected."), {}};
  file_selection_result outcome{result::success(), {}};
  std::size_t total_bytes = 0;
  const std::size_t limit = multiple ? max_selected_files : 1;
  for (auto* item = filenames.values; item; item = item->next) {
    if (outcome.paths.size() == limit)
      return {result::failure("E_DIALOG", "The file chooser returned too many paths."), {}};
    const auto* filename = static_cast<const gchar*>(item->data);
    if (!filename)
      return {result::failure("E_DIALOG", "The file chooser returned no local path."), {}};
    gsize bytes = 0;
    std::unique_ptr<gchar, decltype(&g_free)> utf8(
        g_filename_to_utf8(filename, -1, nullptr, &bytes, nullptr), g_free);
    if (!utf8)
      return {result::failure("E_ENCODING", "The selected path is not valid UTF-8."), {}};
    if (bytes == 0 || bytes > max_selected_path_bytes ||
        bytes > max_selected_total_bytes - total_bytes ||
        !valid_text(std::string_view(utf8.get(), bytes), max_selected_path_bytes) ||
        !g_path_is_absolute(utf8.get()))
      return {result::failure("E_DIALOG", "The file chooser returned an invalid or oversized local path."), {}};
    total_bytes += bytes;
    outcome.paths.emplace_back(utf8.get(), bytes);
  }
  return outcome;
}

bool valid_menu_level(const std::vector<menu_item>& items, std::size_t depth,
                      std::size_t& count, std::set<std::string>& commands) {
  if (depth > 4 || items.size() > 64 - count) return false;
  for (const auto& item : items) {
    if (++count > 64) return false;
    if (item.separator) {
      if (!item.id.empty() || !item.label.empty() || !item.children.empty()) return false;
      continue;
    }
    if (item.label.empty() || !valid_text(item.label, 4096)) return false;
    if (!item.children.empty()) {
      if (!item.id.empty() || item.checked ||
          !valid_menu_level(item.children, depth + 1, count, commands)) return false;
      continue;
    }
    if (item.id.empty() || item.id.size() > 64) return false;
    for (unsigned char character : item.id)
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-')) return false;
    if (!commands.insert(item.id).second) return false;
  }
  return true;
}

bool valid_menu(const std::vector<menu_item>& items) {
  std::size_t count = 0;
  std::set<std::string> commands;
  return valid_menu_level(items, 1, count, commands);
}

struct menu_command {
  std::weak_ptr<linux_state> state;
  std::string id;
  std::uint64_t generation;
  bool tray;
  bool enabled;
};

bool current_menu(const linux_state& state, bool tray, std::uint64_t generation) noexcept {
  return state.active.load() &&
      (tray ? state.tray_menu && state.tray_generation == generation
            : state.popup && state.popup_generation == generation);
}

void menu_activated(GtkMenuItem* item, gpointer data) noexcept {
  const auto& command = *static_cast<menu_command*>(data);
  auto state = command.state.lock();
  if (!state || !command.enabled || !gtk_widget_is_sensitive(GTK_WIDGET(item)) ||
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
                     bool tray, std::uint64_t generation,
                     bool ancestors_enabled = true, bool root = true) {
  GtkWidget* menu = gtk_menu_new();
  g_object_ref_sink(menu);
  try {
    for (const auto& item : items) {
      GtkWidget* row = nullptr;
      if (item.separator) {
        row = gtk_separator_menu_item_new();
      } else {
        if (item.checked) {
          row = gtk_check_menu_item_new_with_label(item.label.c_str());
          gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(row), TRUE);
        } else {
          row = gtk_menu_item_new_with_label(item.label.c_str());
        }
      }
      // Establish ownership before allocating command data or descending into
      // a submenu, so clearing the root also cleans up a partial tree.
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), row);
      if (item.separator) continue;
      const bool enabled = ancestors_enabled && item.enabled;
      gtk_widget_set_sensitive(row, enabled);
      if (!item.children.empty()) {
        GtkWidget* submenu = make_menu(state, item.children, tray, generation, enabled, false);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(row), submenu);
        g_object_unref(submenu);  // The parent menu item now owns the submenu.
      } else {
        auto command = std::make_unique<menu_command>(
            menu_command{state.weak_from_this(), item.id, generation, tray, enabled});
        g_signal_connect_data(row, "activate", G_CALLBACK(menu_activated), command.release(),
            [](gpointer value, GClosure*) { delete static_cast<menu_command*>(value); },
            G_CONNECT_DEFAULT);
      }
    }
    if (root) {
      gtk_menu_attach_to_widget(GTK_MENU(menu), state.window, nullptr);
      gtk_widget_show_all(menu);
    }
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
    g_signal_connect(state.window, "window-state-event", G_CALLBACK(window_state_changed), &state);
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
    auto status = check_evaluation(*state_);
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

  result minimize() override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    gtk_window_iconify(GTK_WINDOW(state_->window));
    return result::success();  // A request, not confirmation from the window manager.
  }

  result maximize() override {
    auto state = state_;
    auto status = check_ui(*state);
    if (!status.ok) return status;
    gtk_window_deiconify(GTK_WINDOW(state->window));
    if (!state->active.load()) return unavailable();
    gtk_window_maximize(GTK_WINDOW(state->window));
    return result::success();
  }

  result restore() override {
    auto state = state_;
    auto status = check_ui(*state);
    if (!status.ok) return status;
    gtk_window_deiconify(GTK_WINDOW(state->window));
    if (!state->active.load()) return unavailable();
    gtk_window_unmaximize(GTK_WINDOW(state->window));
    return result::success();
  }

  window_state_result get_window_state() const override {
    auto status = check_ui(*state_);
    if (!status.ok) return {status, {}};
    // A hidden window need not have been realized yet. Its default false bits
    // are not an observed normal state, so return an explicit failure instead.
    auto* native = gtk_widget_get_window(state_->window);
    if (!gtk_widget_get_realized(state_->window) || !native || gdk_window_is_destroyed(native))
      return {result::failure("E_STATE", "The native window state is not available before realization."), {}};
    return {result::success(), observed_window_state(gdk_window_get_state(native))};
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
    auto state = state_;
    auto status = check_ui(*state);
    if (!status.ok) return {status, {}};
    if (state->modal_busy)
      return {result::failure("E_BUSY", "A modal desktop operation is already active."), {}};
    modal_guard guard(*state);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Choose directory", GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (!dialog)
      return {result::failure("E_DIALOG", "The native directory chooser could not be created."), {}};
    g_object_ref_sink(dialog);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), FALSE);
    state->directory_dialog = dialog;
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    state->directory_dialog = nullptr;
    value_result outcome{result::failure("E_CANCELLED", "Directory selection was cancelled."), {}};
    if (!state->active.load()) {
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

  file_selection_result choose_files(const open_file_options& options) override {
    return choose_file_paths(state_, GTK_FILE_CHOOSER_ACTION_OPEN, options.title,
                             options.extensions, options.multiple);
  }

  value_result choose_save_file(const save_file_options& options) override {
    auto outcome = choose_file_paths(state_, GTK_FILE_CHOOSER_ACTION_SAVE, options.title,
                                    options.extensions, false, options.default_name);
    if (!outcome.status.ok) return {std::move(outcome.status), {}};
    return {result::success(), std::move(outcome.paths.front())};
  }

  result popup_menu(const std::vector<menu_item>& items) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    if (!valid_menu(items))
      return result::failure("E_ARGUMENT", "A popup menu needs a valid tree of at most 64 nodes and 4 levels.");
    if (items.empty()) {
      ++state_->popup_generation;
      clear_menu(state_->popup);
      return result::success();
    }
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
        !valid_text(settings.tooltip, 4096) || !valid_menu(settings.menu))
      return result::failure("E_ARGUMENT", "Invalid native tray label, tooltip, or menu.");
    return install_tray(*state_, settings);
  }

  result clear_tray() override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
    clear_native_tray(*state_);
    return result::success();
  }

  result register_global_shortcut(std::uint64_t token, const global_shortcut& chord) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
    return install_shortcut(*state_, token, chord);
#else
    (void)token;
    (void)chord;
    return result::failure("not_supported", "This Linux build has no X11 global shortcut implementation.");
#endif
  }

  result unregister_global_shortcut(std::uint64_t token) override {
    auto status = check_ui(*state_);
    if (!status.ok) return status;
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
    auto& shortcuts = state_->shortcuts;
    const auto found = std::find_if(shortcuts.begin(), shortcuts.end(),
        [token](const auto& shortcut) { return shortcut->token == token; });
    if (found == shortcuts.end()) return result::success();
    if (ungrab_shortcut(**found))
      return result::failure("E_SHORTCUT_UNREGISTER", "The X11 shortcut was disabled, but native cleanup failed. Retry unregistering it.");
    (*found)->close_connection();
    shortcuts.erase(found);
    return result::success();
#else
    (void)token;
    return result::failure("not_supported", "This Linux build has no X11 global shortcut implementation.");
#endif
  }

  capabilities get_capabilities() const override {
    auto supported = capabilities{"webkitgtk", true, true, true, true, true, true, true};
    supported.window_minimize = true;
    supported.window_maximize = true;
    supported.window_restore = true;
    supported.window_state_observation = true;
    supported.file_open_dialog = true;
    supported.file_save_dialog = true;
#if defined(SOUI_DESKTOP_X11_SHORTCUTS)
    supported.global_shortcuts = x11_shortcuts_available(*state_);
#endif
    return supported;
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
