#include "../../sdk/src/desktop/backend.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

using soui::desktop::detail::backend;
using soui::desktop::detail::backend_events;
using soui::desktop::detail::message_frame;

struct fixture {
  std::unique_ptr<backend> desktop{soui::desktop::detail::make_backend()};
  std::filesystem::path root;
  std::string uri;
  std::thread::id ui{std::this_thread::get_id()};
  int failures{0};
  int messages{0};
  int closed{0};
  int dispatched{0};
  bool loaded{false};
  bool frame_checked{false};
  bool page_checked{false};
  bool evaluated{false};
  bool navigation_checked{false};
  bool cancelled_work_ran{false};

  void expect(bool condition, const char* description) {
    if (!condition) {
      ++failures;
      std::cerr << "FAIL: " << description << '\n';
    }
  }

  void maybe_close() {
    if (!loaded || !frame_checked || !page_checked || !evaluated || !navigation_checked) return;
    expect(desktop->dispatch([this] { cancelled_work_ran = true; }).ok, "queue cancellable work");
    expect(desktop->close().ok, "close succeeds inside a queued callback");
  }
};

gboolean watchdog(gpointer data) {
  auto& test = *static_cast<fixture*>(data);
  test.expect(false, "real WebKit backend completed before the watchdog");
  (void)test.desktop->close();
  (void)test.desktop->quit_loop();
  return G_SOURCE_REMOVE;
}

gboolean cancel_directory_dialog(gpointer) {
  GList* windows = gtk_window_list_toplevels();
  for (GList* it = windows; it; it = it->next) {
    if (GTK_IS_FILE_CHOOSER_DIALOG(it->data))
      gtk_dialog_response(GTK_DIALOG(it->data), GTK_RESPONSE_CANCEL);
  }
  g_list_free(windows);
  return G_SOURCE_REMOVE;
}

gboolean verify_navigation(gpointer data) {
  auto& test = *static_cast<fixture*>(data);
  int webview_windows = 0;
  GList* windows = gtk_window_list_toplevels();
  for (GList* it = windows; it; it = it->next) {
    if (!GTK_IS_BIN(it->data)) continue;
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(it->data));
    if (!child || !WEBKIT_IS_WEB_VIEW(child)) continue;
    ++webview_windows;
    const char* actual = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(child));
    test.expect(actual && test.uri == actual, "network navigation leaves the selected document intact");
  }
  g_list_free(windows);
  test.expect(webview_windows == 1, "window.open does not create another WebView");
  test.navigation_checked = true;
  test.maybe_close();
  return G_SOURCE_REMOVE;
}

}  // namespace

int main() {
  fixture test;
  gchar* directory = g_dir_make_tmp("soui-linux-backend-XXXXXX", nullptr);
  if (!directory) return 2;
  test.root = directory;
  g_free(directory);
  const auto entry = test.root / "index.html";
  {
    std::ofstream html(entry);
    html << R"HTML(<!doctype html><meta charset="utf-8"><title>Linux integration</title>
<script>
if (self === top) {
  window.addEventListener("DOMContentLoaded", function () {
    window.webkit.messageHandlers.soui.postMessage(
      window.__linuxPreload === "top-only" ? "page-preload-ok" : "page-preload-missing");
    var child = document.createElement("iframe");
    child.id = "child";
    document.body.appendChild(child);
    window.webkit.messageHandlers.soui.postMessage(
      typeof child.contentWindow.__linuxPreload === "undefined" ?
        "frame-preload-absent" : "frame-preload-present");
  });
}
</script>
<body>Linux backend integration</body>
)HTML";
  }
  gchar* uri = g_filename_to_uri(entry.c_str(), nullptr, nullptr);
  if (!uri) return 2;
  test.uri = uri;
  g_free(uri);
  soui::desktop::options options;
  options.resource_root = test.root;
  options.title = "SOUI Linux backend integration";
  options.width = 640;
  options.height = 480;
  backend_events events;
  events.allow_navigation = [&](std::string_view uri) { return uri == test.uri; };
  events.message = [&](std::string_view message, std::string_view uri, message_frame frame) {
    ++test.messages;
    test.expect(std::this_thread::get_id() == test.ui, "message callback uses the UI thread");
    test.expect(frame == message_frame::unknown, "GTK never invents a sender-frame proof");
    test.expect(uri == test.uri, "message context is the native-selected document");
    if (message == "page-preload-ok") test.page_checked = true;
    else if (message == "frame-preload-absent") test.frame_checked = true;
    else if (message == "evaluated") test.evaluated = true;
    else test.expect(false, "preload and evaluated message content");
    test.maybe_close();
  };
  events.closed = [&] { ++test.closed; (void)test.desktop->quit_loop(); };
  events.error = [&](const soui::desktop::result& result) {
    ++test.failures;
    std::cerr << result.code << ": " << result.message << '\n';
    (void)test.desktop->close();
  };
  events.loaded = [&](std::string_view uri) {
    test.expect(uri == test.uri, "load notification has the selected URI");
    test.expect(!test.loaded, "one load notification for the original document");
    test.loaded = true;
    // The backend must dispatch load callbacks off the WebKit signal stack:
    // these operations enter a nested GTK loop without blocking its IPC reply.
    test.expect(test.desktop->clipboard_write("SOUI clipboard \xE4\xB8\xAD\xE6\x96\x87").ok,
                "real native clipboard write");
    auto clipboard = test.desktop->clipboard_read();
    test.expect(clipboard.status.ok && clipboard.value == "SOUI clipboard \xE4\xB8\xAD\xE6\x96\x87",
                "real native clipboard read");
    g_idle_add(cancel_directory_dialog, nullptr);
    auto directory_result = test.desktop->choose_directory();
    test.expect(!directory_result.status.ok && directory_result.status.code == "E_CANCELLED",
                "native directory dialog cancellation");
    test.expect(test.desktop->evaluate(
        "window.webkit.messageHandlers.soui.postMessage('evaluated');"
        "window.open('https://example.invalid/new-window');"
        "location.assign('https://example.invalid/navigation');").ok, "real JavaScript evaluation");
    g_timeout_add(200, verify_navigation, &test);
    test.maybe_close();
  };
  auto created = test.desktop->create(options, std::move(events));
  if (!created.ok) {
    std::cerr << created.code << ": " << created.message << '\n';
    std::filesystem::remove_all(test.root);
    return 2;  // Missing display/dependencies is a failure, never a skipped pass.
  }
  test.expect(!test.desktop->has_message_frame_metadata(), "native frame metadata capability is honest");
  const auto capabilities = test.desktop->get_capabilities();
  test.expect(capabilities.backend == "webkitgtk" && capabilities.clipboard_read &&
              capabilities.clipboard_write && capabilities.directory_dialog &&
              capabilities.window_resize && capabilities.window_focus &&
              capabilities.popup_menu && capabilities.tray, "native capability report");
  test.expect(test.desktop->set_title("SOUI integration \xE4\xB8\xAD\xE6\x96\x87").ok, "UTF-8 title");
  test.expect(test.desktop->resize(700, 500).ok, "native resize");
  test.expect(test.desktop->show(false).ok && test.desktop->show(true).ok, "hide and show");
  test.expect(test.desktop->focus().ok, "native focus request");
  test.expect(!test.desktop->resize(0, 500).ok, "reject invalid dimensions");
  test.expect(!test.desktop->load("https://example.invalid/", "void 0").ok, "reject remote native load");
  test.expect(test.desktop->load(test.uri, "window.__linuxPreload = 'top-only';").ok, "native local load");
  std::atomic<bool> submitted{false};
  std::thread worker([&] {
    auto result = test.desktop->dispatch([&] {
      test.expect(std::this_thread::get_id() == test.ui, "worker dispatch runs on the UI thread");
      ++test.dispatched;
    });
    submitted.store(result.ok);
  });
  worker.join();
  test.expect(submitted.load() && test.dispatched == 0, "dispatch never runs synchronously on its worker");
  guint timeout = g_timeout_add_seconds(20, watchdog, &test);
  test.expect(test.desktop->run().ok, "real GTK event loop exits");
  // The watchdog may have removed itself on failure; remove only a live source.
  if (g_main_context_find_source_by_id(nullptr, timeout)) g_source_remove(timeout);
  test.expect(test.closed == 1, "one closed callback");
  test.expect(test.dispatched == 1, "one UI dispatch execution");
  test.expect(!test.cancelled_work_ran, "close discards queued UI work");
  test.expect(test.messages == 3, "all expected page/frame/evaluation messages");
  test.expect(!test.desktop->dispatch([] {}).ok, "dispatch after close rejects");
  test.expect(!test.desktop->evaluate("void 0").ok, "evaluation after close rejects");
  test.expect(test.desktop->close().ok && test.closed == 1, "close is idempotent");
  test.desktop.reset();

  // A renderer reload must ask common code for fresh authorization rather than
  // letting WebKit reinject the previous document's preload. The private-backend
  // fixture models that callback with a visibly different preload generation.
  {
    std::ofstream html(entry);
    html << R"HTML(<!doctype html><meta charset="utf-8"><script>
window.addEventListener("DOMContentLoaded", function () {
  window.webkit.messageHandlers.soui.postMessage(window.__linuxPreload);
});
</script><body>Reload lifecycle</body>)HTML";
  }
  test.desktop = soui::desktop::detail::make_backend();
  int reloads = 0;
  int reload_loads = 0;
  bool fresh_message = false;
  bool old_message = false;
  bool reload_closed = false;
  backend_events reload_events;
  reload_events.allow_navigation = [&](std::string_view uri) { return uri == test.uri; };
  reload_events.message = [&](std::string_view message, std::string_view, message_frame) {
    if (message == "old-generation") old_message = true;
    else if (message == "fresh-generation") fresh_message = true;
    else if (message == "stale-after-reload") {
      // location.reload() requests asynchronous navigation. A message sent in
      // the same JS task may reach the host before its navigation policy
      // callback observes that request. Authorization is revoked at the native
      // reload boundary, not retroactively when JS calls location.reload().
      test.expect(reloads == 0, "old-document messages never cross the native reload boundary");
    } else test.expect(false, "only expected reload fixture messages are delivered");
    if (fresh_message && reload_loads == 2) (void)test.desktop->close();
  };
  reload_events.loaded = [&](std::string_view) {
    ++reload_loads;
    if (reload_loads == 1) {
      test.expect(test.desktop->evaluate(
          "location.reload();window.webkit.messageHandlers.soui.postMessage('stale-after-reload');").ok,
          "request renderer reload");
    } else if (reload_loads == 2) {
      if (fresh_message) (void)test.desktop->close();
    } else {
      test.expect(false, "no renderer reload loop");
      (void)test.desktop->close();
    }
  };
  reload_events.reload = [&] {
    ++reloads;
    test.expect(test.desktop->load(test.uri, "window.__linuxPreload='fresh-generation';").ok,
                "reload callback provides a fresh native preload");
  };
  reload_events.closed = [&] { reload_closed = true; (void)test.desktop->quit_loop(); };
  reload_events.error = [&](const soui::desktop::result& error) {
    test.expect(false, error.message.c_str());
    (void)test.desktop->close();
  };
  test.expect(test.desktop->create(options, std::move(reload_events)).ok, "create the reload fixture");
  test.expect(test.desktop->load(test.uri, "window.__linuxPreload='old-generation';").ok,
              "load the initial reload fixture");
  timeout = g_timeout_add_seconds(20, watchdog, &test);
  test.expect(test.desktop->run().ok, "reload event loop exits");
  if (g_main_context_find_source_by_id(nullptr, timeout)) g_source_remove(timeout);
  test.expect(reloads == 1 && reload_loads == 2 && old_message && fresh_message && reload_closed,
              "renderer reload rotates the preload and completes exactly once");
  test.desktop.reset();

  // The real loop owner can lose its native window before run(), while another
  // managed backend must still load, exchange messages, and stop that loop.
  // This is native GTK/WebKit coverage, not the fake common-layer lifecycle test.
  {
    std::ofstream html(entry);
    html << "<!doctype html><meta charset=\"utf-8\"><body>Managed child</body>";
  }
  test.desktop = soui::desktop::detail::make_backend();
  auto child = soui::desktop::detail::make_backend();
  int root_closed = 0;
  int child_closed = 0;
  int child_messages = 0;
  int child_script_errors = 0;
  backend_events root_events;
  root_events.closed = [&] { ++root_closed; };
  test.expect(test.desktop->create(options, std::move(root_events)).ok,
              "create a real managed-loop owner");
  backend_events child_events;
  child_events.allow_navigation = [&](std::string_view uri) { return uri == test.uri; };
  child_events.loaded = [&](std::string_view) {
    test.expect(child->evaluate("if (true)").ok,
                "submit incomplete script without silently completing its syntax");
  };
  child_events.message = [&](std::string_view message, std::string_view, message_frame) {
    test.expect(message == "managed-child", "child message remains independent of the closed root");
    ++child_messages;
    (void)child->close();
  };
  child_events.closed = [&] {
    ++child_closed;
    (void)test.desktop->quit_loop();
  };
  child_events.error = [&](const soui::desktop::result& error) {
    test.expect(error.code == "E_JAVASCRIPT", "actual script failures retain their error category");
    ++child_script_errors;
    if (child_script_errors == 1) {
      test.expect(child->evaluate("throw Promise.resolve(1);").ok,
                  "a thrown Promise is an exception, not a discarded return value");
    } else if (child_script_errors == 2) {
      test.expect(child->evaluate(
          "window.webkit.messageHandlers.soui.postMessage('managed-child');").ok,
          "child evaluates after real script errors and closed root");
    } else {
      test.expect(false, error.message.c_str());
      (void)child->close();
    }
  };
  test.expect(child->create(options, std::move(child_events)).ok, "create a real managed child");
  test.expect(child->load(test.uri, "void 0").ok, "load the independently managed child");
  test.expect(test.desktop->close().ok && root_closed == 1, "close only the native root before run");
  timeout = g_timeout_add_seconds(20, watchdog, &test);
  test.expect(test.desktop->run().ok, "closed root still owns the shared native loop");
  if (g_main_context_find_source_by_id(nullptr, timeout)) g_source_remove(timeout);
  test.expect(root_closed == 1 && child_closed == 1 && child_messages == 1 && child_script_errors == 2,
              "child stays operational until it closes and stops the shared loop");
  child.reset();
  test.desktop.reset();
  std::filesystem::remove_all(test.root);
  if (test.failures) return 1;
  std::cout << "Linux WebKitGTK backend integration passed.\n";
  return 0;
}
