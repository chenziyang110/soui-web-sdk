#include <soui/desktop/app.h>
#include "../../sdk/src/desktop/backend.h"
#include "../../sdk/src/desktop/protocol.h"
#include "../../sdk/src/desktop/resource_uri.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace soui::desktop;
using soui::desktop::detail::message_frame;
using nlohmann::json;
int failures = 0;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; } } while (false)

result unsupported() { return result::failure("test_unsupported", "Fake capability is unavailable."); }

class fake_backend;
std::vector<fake_backend*> backends;
std::function<void()> during_next_create;
struct native_counts {
  int closed{0}, loop_quits{0}, runs{0}, popup_calls{0}, tray_calls{0}, clear_tray_calls{0};
};

// A deliberately simple transport fake. No platform API is called. Tests may
// trigger callbacks reentrantly to model a modal native message loop.
class fake_backend final : public detail::backend {
 public:
  fake_backend() { backends.push_back(this); }
  ~fake_backend() override { backends.erase(std::remove(backends.begin(), backends.end(), this), backends.end()); }
  detail::backend_events events;
  std::shared_ptr<native_counts> counts = std::make_shared<native_counts>();
  std::atomic<bool> active{false};
  bool metadata{true};
  std::string uri, preload;
  std::vector<std::string> evaluations;
  result evaluation_status{result::success()};
  result dispatch_status{result::success()};
  result load_status{result::success()};
  result menu_status{unsupported()}, tray_status{unsupported()}, clear_tray_status{unsupported()};
  std::vector<menu_item> popup_items;
  tray_options tray_settings;
  std::function<void()> while_popup;
  result shortcut_register_status{result::success()}, shortcut_unregister_status{result::success()};
  std::vector<std::pair<std::uint64_t, global_shortcut>> shortcut_registrations;
  std::vector<std::uint64_t> shortcut_unregistrations;
  std::function<void(std::uint64_t)> while_shortcut_register, while_shortcut_unregister;
  std::mutex mutex;
  std::condition_variable task_ready;
  std::deque<std::function<void()>> tasks;
  int loads{0};
  int minimize_calls{0}, maximize_calls{0}, restore_calls{0};
  result window_status{result::success()};
  window_state_result observed{result::success(), {}};
  int service_calls{0};
  result service_status{unsupported()};
  value_result clipboard_value{unsupported(), {}}, directory_value{unsupported(), {}};
  std::string service_text;
  int service_width{0}, service_height{0};
  bool service_visible{false};
  std::function<void()> while_service;
  void service_enter() { ++service_calls; if (while_service) while_service(); }

  result create(const options&, detail::backend_events callbacks) override {
    events = std::move(callbacks); active.store(true);
    auto nested = std::move(during_next_create);
    during_next_create = {};
    if (nested) nested();
    return result::success();
  }
  result load(std::string selected, std::string script) override {
    ++loads; uri = std::move(selected); preload = std::move(script); return load_status;
  }
  result run() override {
    ++counts->runs;
    // A fake shared loop pumps each existing window. It deliberately does not
    // stop merely because the root window's own active flag is false.
    const auto windows = backends;
    for (auto* window : windows)
      if (std::find(backends.begin(), backends.end(), window) != backends.end()) window->flush();
    return result::success();
  }
  result quit_loop() override { ++counts->loop_quits; return result::success(); }
  result close() override {
    if (!active.exchange(false)) return result::success();
    ++counts->closed;
    { std::lock_guard<std::mutex> lock(mutex); tasks.clear(); }
    const auto callback = events.closed;
    if (callback) callback();
    return result::success();
  }
  result evaluate(std::string script) override {
    if (evaluation_status.ok) evaluations.push_back(std::move(script));
    return evaluation_status;
  }
  result dispatch(std::function<void()> work) override {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active.load()) return result::failure("closed", "Fake backend is closed.");
    if (!dispatch_status.ok) return dispatch_status;
    tasks.push_back(std::move(work)); task_ready.notify_all(); return result::success();
  }
  bool wait_tasks(std::size_t count = 1) {
    std::unique_lock<std::mutex> lock(mutex);
    return task_ready.wait_for(lock, std::chrono::seconds(5), [&] { return tasks.size() >= count; });
  }
  void await_asset() { CHECK(wait_tasks()); flush(); }
  void flush() {
    for (;;) {
      std::function<void()> work;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (tasks.empty()) return;
        work = std::move(tasks.front()); tasks.pop_front();
      }
      if (active.load()) work();
    }
  }
  result set_title(std::string_view text) override { service_text = text; service_enter(); return service_status; }
  result resize(int width, int height) override {
    service_width = width; service_height = height; service_enter(); return service_status;
  }
  result show(bool visible) override { service_visible = visible; service_enter(); return service_status; }
  result focus() override { service_enter(); return service_status; }
  result minimize() override { ++minimize_calls; return window_status; }
  result maximize() override { ++maximize_calls; return window_status; }
  result restore() override { ++restore_calls; return window_status; }
  window_state_result get_window_state() const override { return observed; }
  value_result clipboard_read() override { service_enter(); return clipboard_value; }
  result clipboard_write(std::string_view text) override { service_text = text; service_enter(); return service_status; }
  value_result choose_directory() override { service_enter(); return directory_value; }
  result popup_menu(const std::vector<menu_item>& items) override {
    ++counts->popup_calls; popup_items = items;
    if (while_popup) while_popup();
    return menu_status;
  }
  result set_tray(const tray_options& settings) override {
    ++counts->tray_calls; tray_settings = settings; return tray_status;
  }
  result clear_tray() override { ++counts->clear_tray_calls; return clear_tray_status; }
  result register_global_shortcut(std::uint64_t token, const global_shortcut& chord) override {
    shortcut_registrations.emplace_back(token, chord);
    if (while_shortcut_register) while_shortcut_register(token);
    return shortcut_register_status;
  }
  result unregister_global_shortcut(std::uint64_t token) override {
    shortcut_unregistrations.push_back(token);
    if (while_shortcut_unregister) while_shortcut_unregister(token);
    return shortcut_unregister_status;
  }
  capabilities get_capabilities() const override {
    capabilities value{"test-fake", false, false, false, false, false, menu_status.ok, tray_status.ok};
    value.global_shortcuts = true;
    return value;
  }
  bool has_message_frame_metadata() const override { return metadata; }

  std::string token() const {
    const auto begin = preload.find("const token=");
    if (begin == std::string::npos) throw std::runtime_error("Missing generated token");
    const auto first = begin + std::string("const token=").size();
    const auto end = preload.find(';', first);
    return json::parse(preload.substr(first, end - first)).get<std::string>();
  }
  json envelope(std::uint64_t id, std::string method = "echo", json params = nullptr) const {
    return {{"v", 1}, {"token", token()}, {"id", id}, {"method", std::move(method)}, {"params", std::move(params)}};
  }
  void send(const json& value, message_frame frame = message_frame::main,
            std::string source = {}) {
    const auto callback = events.message;
    const auto text = value.dump();
    callback(text, source.empty() ? uri : source, frame);
  }
  void loaded() { const auto callback = events.loaded; callback(uri); }
  void command(std::string_view id) {
    const auto callback = events.command;
    if (callback) callback(id);
  }
  void shortcut(std::uint64_t token) {
    // Model backend deferral off the native OS callback stack. This queue is
    // deliberately independent of the renderer-dependent dispatch status.
    std::lock_guard<std::mutex> lock(mutex);
    if (!active.load()) return;
    const auto callback = events.global_shortcut;
    tasks.push_back([callback, token] { if (callback) callback(token); });
  }
  json last_response() const {
    if (evaluations.empty()) throw std::runtime_error("Missing evaluated response");
    const auto& script = evaluations.back();
    const auto begin = script.find("JSON.parse(");
    if (begin == std::string::npos || script.size() < begin + 14)
      throw std::runtime_error("Responses must use JSON.parse, not object literals");
    const auto encoded = script.substr(begin + 11, script.size() - begin - 14);
    return json::parse(json::parse(encoded).get<std::string>());
  }
};
fake_backend* latest = nullptr;

struct files {
  std::filesystem::path base, root;
  files() {
    base = std::filesystem::temp_directory_path() /
        ("soui-desktop-contract-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(base)) throw std::runtime_error("Cannot create isolated fixture");
    root = base / std::filesystem::u8path("resources space # % 中文");
    std::filesystem::create_directory(root);
    write(root / "index.html");
    write(root / "second.html");
    write(root / "upper.HTM");
    write(root / "not-html.txt");
    std::filesystem::create_directory(base / "resources-other");
    write(base / "outside.html");
    write(base / "resources-other" / "index.html");
  }
  static void write(const std::filesystem::path& path) {
    std::ofstream stream(path); stream << "<!doctype html><meta charset=\"utf-8\">";
    if (!stream) throw std::runtime_error("Cannot write test fixture");
  }
  ~files() { std::error_code ignored; std::filesystem::remove_all(base, ignored); }
  options settings() const { options out; out.resource_root = root; out.visible = false; return out; }
};

void desktop_services_contract(files& fixture) {
  const std::vector<std::pair<std::string, json>> methods = {
    {"desktop.window.setTitle", {{"title", "Title"}}},
    {"desktop.window.resize", {{"width", 640}, {"height", 480}}},
    {"desktop.window.show", {{"visible", true}}},
    {"desktop.window.focus", json::object()},
    {"desktop.clipboard.readText", json::object()},
    {"desktop.clipboard.writeText", {{"text", "Text"}}},
    {"desktop.dialog.chooseDirectory", json::object()}
  };
  // Each grant is independent, including from the older window-control grant.
  for (int permission = -1; permission < 4; ++permission) {
    auto settings = fixture.settings();
    settings.enable_window_controls = true;
    settings.services.window_properties = permission == 0;
    settings.services.clipboard_read = permission == 1;
    settings.services.clipboard_write = permission == 2;
    settings.services.directory_dialog = permission == 3;
    int business = 0;
    callbacks handlers;
    handlers.invoke = [&](std::string_view, std::string_view) { ++business; return "42"; };
    application app(settings, handlers);
    CHECK(app.initialize().ok);
    auto& native = *latest;
    native.service_status = result::success();
    native.clipboard_value = {result::success(), ""};
    native.directory_value = {result::success(), "/chosen"};
    std::uint64_t id = 0;
    for (std::size_t i = 0; i < methods.size(); ++i) {
      const bool allowed = permission == (i < 4 ? 0 : static_cast<int>(i) - 3);
      const auto before = native.service_calls;
      native.send(native.envelope(++id, methods[i].first, methods[i].second));
      const auto response = native.last_response();
      CHECK(response["ok"] == allowed);
      CHECK(native.service_calls == before + (allowed ? 1 : 0));
      if (!allowed) CHECK(response["error"]["code"] == "permission_denied");
    }
    CHECK(business == 0);
    for (const char* method : {"desktop.window.custom", "desktop.clipboard.custom", "desktop.dialog.custom"}) {
      native.send(native.envelope(++id, method));
      CHECK(native.last_response()["result"] == 42);
    }
    CHECK(business == 3);
  }
  auto settings = fixture.settings();
  settings.services = {true, true, true, true};
  application app(settings);
  CHECK(app.initialize().ok);
  auto& native = *latest;
  native.service_status = result::success();
  std::uint64_t id = 0;
  const auto call = [&](const std::string& method, json params) {
    native.send(native.envelope(++id, method, std::move(params)));
    return native.last_response();
  };
  for (const auto& method : methods) {
    const auto before = native.service_calls;
    CHECK(call(method.first, nullptr)["error"]["code"] == "invalid_params");
    auto extra = method.second; extra["windowId"] = 123;
    CHECK(call(method.first, extra)["error"]["code"] == "invalid_params");
    CHECK(native.service_calls == before);
  }
  const auto invalid = [&](const std::string& method, json params) {
    const auto before = native.service_calls;
    CHECK(call(method, std::move(params))["error"]["code"] == "invalid_params");
    CHECK(native.service_calls == before);
  };
  invalid(methods[0].first, {{"title", 1}});
  invalid(methods[0].first, {{"title", std::string(4097, 'x')}});
  invalid(methods[0].first, {{"title", std::string("a\0b", 3)}});
  invalid(methods[0].first, json::object());
  for (const json& width : std::vector<json>{63, 16385, -1, 64.0, true, "64", UINT64_MAX})
    invalid(methods[1].first, {{"width", width}, {"height", 64}});
  invalid(methods[1].first, {{"width", 64}, {"height", 16385}});
  invalid(methods[1].first, {{"width", 64}});
  invalid(methods[2].first, {{"visible", 1}});
  invalid(methods[5].first, {{"text", false}});
  invalid(methods[5].first, {{"text", std::string(32769, 'x')}});
  invalid(methods[5].first, {{"text", std::string("\0", 1)}});
  CHECK(call(methods[0].first, {{"title", std::string(4096, 'x')}})["result"] == json({{"accepted", true}}));
  CHECK(native.service_text.size() == 4096);
  CHECK(call(methods[1].first, {{"width", 64}, {"height", 16384}})["ok"] == true);
  CHECK(native.service_width == 64 && native.service_height == 16384);
  CHECK(call(methods[2].first, {{"visible", false}})["ok"] == true && !native.service_visible);
  CHECK(call(methods[3].first, json::object())["result"] == json({{"accepted", true}}));
  CHECK(call(methods[5].first, {{"text", std::string(32768, 'x')}})["ok"] == true);
  CHECK(native.service_text.size() == 32768);
  CHECK(call(methods[5].first, {{"text", ""}})["ok"] == true && native.service_text.empty());
  for (const auto& text : {std::string(), std::string(65536, '\n'), std::string(u8"中文")}) {
    native.clipboard_value = {result::success(), text};
    CHECK(call(methods[4].first, json::object())["result"] == json({{"text", text}}));
  }
  for (const auto& text : {std::string(65537, 'x'), std::string("a\0b", 3), std::string("\xC0\xAF", 2)}) {
    native.clipboard_value = {result::success(), text};
    CHECK(call(methods[4].first, json::object())["error"]["code"] == "invalid_result");
    native.directory_value = {result::success(), text};
    CHECK(call(methods[6].first, json::object())["error"]["code"] == "invalid_result");
  }
  native.directory_value = {result::success(), ""};
  CHECK(call(methods[6].first, json::object())["error"]["code"] == "invalid_result");
  native.directory_value = {result::success(), std::string(65536, 'x')};
  CHECK(call(methods[6].first, json::object())["result"] ==
    json({{"cancelled", false}, {"path", native.directory_value.value}}));
  native.directory_value = {result::failure("E_CANCELLED", "User cancelled."), "ignored"};
  CHECK(call(methods[6].first, json::object())["result"] == json({{"cancelled", true}, {"path", ""}}));
  native.service_status = result::failure("native_denied", "OS denied access.");
  native.clipboard_value = native.directory_value = {native.service_status, "ignored"};
  for (const auto& method : methods) {
    const auto response = call(method.first, method.second);
    CHECK(response["error"] == json({{"code", "native_denied"}, {"message", "OS denied access."}}));
    CHECK(!response.contains("result"));
  }
  native.directory_value.status.code = "cancelled";
  CHECK(call(methods[6].first, json::object())["error"]["code"] == "cancelled");
}

void desktop_service_reentry_contract(files& fixture) {
  auto settings = fixture.settings(); settings.services.directory_dialog = true;
  bool in_service = false;
  int business = 0, closed = 0, ready = 0;
  callbacks handlers;
  handlers.invoke = [&](std::string_view, std::string_view) { CHECK(!in_service); ++business; return "null"; };
  handlers.closed = [&] { CHECK(!in_service); ++closed; };
  handlers.ready = [&] { CHECK(!in_service); ++ready; };
  application app(settings, handlers);
  CHECK(app.initialize().ok);
  auto& native = *latest;
  native.directory_value = {result::success(), "/chosen"};
  native.while_service = [&] {
    in_service = true;
    native.send(native.envelope(2)); native.loaded();
    CHECK(business == 0 && ready == 0);
    in_service = false;
  };
  native.send(native.envelope(1, "desktop.dialog.chooseDirectory", json::object()));
  CHECK(business == 1 && ready == 1 && native.evaluations.size() == 2);
  // Even exceptional native exits release callback serialization.
  native.while_service = [&] { native.send(native.envelope(4)); throw std::runtime_error("native failure"); };
  native.send(native.envelope(3, "desktop.dialog.chooseDirectory", json::object()));
  CHECK(business == 2 && native.evaluations.size() == 4);
  native.while_service = [&] {
    in_service = true;
    native.send(native.envelope(6));
    CHECK(app.navigate("second.html").ok);
    native.loaded();
    CHECK(ready == 1);
    in_service = false;
  };
  native.send(native.envelope(5, "desktop.dialog.chooseDirectory", json::object()));
  CHECK(business == 2 && ready == 2 && native.evaluations.size() == 4);
  native.while_service = [&] {
    in_service = true;
    native.send(native.envelope(2));
    CHECK(app.close().ok);
    CHECK(closed == 0);
    in_service = false;
  };
  native.send(native.envelope(1, "desktop.dialog.chooseDirectory", json::object()));
  CHECK(business == 2 && closed == 1 && native.evaluations.size() == 4);
}

void window_state_contract(files& fixture) {
  int business_calls = 0, state_calls = 0;
  application* app = nullptr;
  fake_backend* native = nullptr;
  callbacks handlers;
  handlers.invoke = [&](std::string_view method, std::string_view) {
    ++business_calls;
    if (method == "defer") {
      native->events.state_changed({true, false});
      native->events.state_changed({true, true});
      CHECK(state_calls == 0); // No nested user callback; consecutive observations coalesce.
    } else if (method == "navigate") {
      native->events.state_changed({false, true});
      CHECK(app->navigate("second.html").ok);
      native->loaded();
    } else if (method == "close") {
      native->events.state_changed({true, false});
      CHECK(app->close().ok);
    }
    return "null";
  };
  bool close_on_state = false;
  handlers.state_changed = [&](window_id id, window_state value) {
    ++state_calls;
    CHECK(id == app->id());
    CHECK(value.minimized || value.maximized);
    if (close_on_state) CHECK(app->close().ok);
  };
  auto settings = fixture.settings();
  callbacks disabled_handlers;
  disabled_handlers.invoke = [&](std::string_view, std::string_view) { ++business_calls; return "null"; };
  application root(settings, disabled_handlers);
  CHECK(root.minimize().code == "closed");
  CHECK(root.get_window_state().status.code == "closed");
  CHECK(root.initialize().ok);
  auto* disabled = latest;
  for (const auto* method : {"minimize", "maximize", "restore", "getState"}) {
    disabled->send(disabled->envelope(disabled->evaluations.size() + 1,
      std::string("desktop.window.") + method, json::object()));
    CHECK(disabled->last_response()["error"]["code"] == "permission_denied");
  }
  CHECK(business_calls == 0);
  // Only the four standardized methods are reserved, not the whole prefix.
  disabled->send(disabled->envelope(5, "desktop.window.unknown", {{"business", true}}));
  CHECK(disabled->last_response()["ok"] == true && disabled->last_response()["result"].is_null());
  CHECK(business_calls == 1);
  CHECK(root.minimize().ok); // Native controls do not require page permission.
  CHECK(disabled->minimize_calls == 1);
  disabled->loaded();
  const auto disabled_count = disabled->evaluations.size();
  disabled->events.state_changed({true, true});
  CHECK(disabled->evaluations.size() == disabled_count);

  settings.enable_window_controls = true;
  auto child = root.create_window(settings, handlers);
  CHECK(child.status.ok);
  app = child.window.get(); native = latest;
  native->loaded();
  std::uint64_t id = 0;
  const auto send = [&](std::string method, json params = json::object()) {
    native->send(native->envelope(++id, std::move(method), std::move(params)));
  };
  auto untrusted = native->envelope(1, "desktop.window.minimize", json::object());
  native->send(untrusted, message_frame::subframe);
  native->send(untrusted, message_frame::main, "https://example.invalid/");
  untrusted["token"] = disabled->token();
  native->send(untrusted);
  CHECK(native->evaluations.empty() && native->minimize_calls == 0);
  for (const auto* method : {"minimize", "maximize", "restore"}) {
    send(std::string("desktop.window.") + method);
    CHECK(native->last_response()["result"] == json({{"accepted", true}}));
  }
  CHECK(native->minimize_calls == 1 && native->maximize_calls == 1 && native->restore_calls == 1);
  CHECK(state_calls == 0); // Accepted requests do not synthesize observations.
  native->observed.value = {true, true}; // OS observations need not be exclusive.
  CHECK(app->get_window_state().value.minimized && app->get_window_state().value.maximized);
  send("desktop.window.getState");
  CHECK(native->last_response()["result"] == json({{"minimized", true}, {"maximized", true}}));
  native->observed.status = unsupported();
  send("desktop.window.getState");
  CHECK(native->last_response()["error"]["code"] == "test_unsupported");
  CHECK(!native->last_response().contains("result"));
  native->window_status = unsupported();
  send("desktop.window.restore");
  CHECK(native->last_response()["error"]["code"] == "test_unsupported");
  for (const auto& params : {json(nullptr), json::array(), json({{"windowId", app->id()}})}) {
    send("desktop.window.minimize", params);
    CHECK(native->last_response()["error"]["code"] == "invalid_params");
  }
  CHECK(business_calls == 1); // Standard methods never fall through to business code.
  send("desktop.window.unknown", {{"business", true}});
  CHECK(native->last_response()["ok"] == true && native->last_response()["result"].is_null());
  CHECK(business_calls == 2 && native->minimize_calls == 1);
  std::thread worker([&] {
    CHECK(app->minimize().code == "wrong_thread");
    CHECK(app->maximize().code == "wrong_thread");
    CHECK(app->restore().code == "wrong_thread");
    CHECK(app->get_window_state().status.code == "wrong_thread");
  });
  worker.join();
  send("defer");
  CHECK(state_calls == 1);
  CHECK(native->last_response()["event"] == "desktop.window.stateChanged");
  CHECK(native->last_response()["value"] == json({{"minimized", true}, {"maximized", true}}));
  const auto count = native->evaluations.size();
  send("navigate");
  CHECK(state_calls == 2); // Native observation survives navigation.
  CHECK(native->evaluations.size() == count); // Old page event and response do not.
  id = 0;
  CHECK(app->reload().ok);
  native->events.state_changed({false, true});
  CHECK(state_calls == 3 && native->evaluations.size() == count); // Not ready.
  native->loaded();
  close_on_state = true;
  native->events.state_changed({true, false});
  CHECK(state_calls == 4 && native->evaluations.size() == count); // Callback close barrier.
  native->events.state_changed({false, true});
  CHECK(state_calls == 4);
  CHECK(app->maximize().code == "closed" && app->restore().code == "closed");
  CHECK(app->get_window_state().status.code == "closed");

  close_on_state = false;
  auto closing = root.create_window(settings, handlers);
  CHECK(closing.status.ok);
  app = closing.window.get(); native = latest; native->loaded(); id = 0;
  send("close");
  CHECK(state_calls == 4 && native->evaluations.empty()); // Pending observations discarded.
}

void asset_bridge_contract(files& fixture) {
  { std::ofstream out(fixture.root / "config.json", std::ios::binary); out << "{\"ok\":true}"; }
  int business_calls = 0;
  callbacks handlers;
  handlers.invoke = [&](std::string_view, std::string_view) { ++business_calls; return "null"; };
  auto settings = fixture.settings();
  application root(settings, handlers);
  CHECK(root.initialize().ok);
  auto* disabled = latest;
  disabled->send(disabled->envelope(1, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(disabled->last_response()["error"]["code"] == "asset_read_disabled");
  CHECK(business_calls == 0);
  settings.enable_asset_read = true;
  auto child = root.create_window(settings, handlers);
  CHECK(child.status.ok);
  auto* enabled = latest;
  enabled->send(enabled->envelope(1, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(enabled->evaluations.empty());
  enabled->await_asset();
  CHECK(enabled->last_response()["ok"] == true);
  CHECK(enabled->last_response()["result"]["mime"] == "application/json");
  CHECK(enabled->last_response()["result"]["size"] == 11);
  CHECK(enabled->last_response()["result"]["base64"] == "eyJvayI6dHJ1ZX0=");
  CHECK(business_calls == 0);
  enabled->send(enabled->envelope(2, "soui.asset.read", {{"path", "../outside.html"}}));
  enabled->await_asset();
  CHECK(enabled->last_response()["ok"] == false);
  enabled->send(enabled->envelope(3, "soui.asset.read", {{"path", "config.json"}, {"extra", true}}));
  CHECK(enabled->last_response()["error"]["code"] == "invalid_asset_path");
  const auto count = enabled->evaluations.size();
  auto stale = enabled->envelope(4, "soui.asset.read", {{"path", "config.json"}});
  auto foreign = stale;
  foreign["token"] = disabled->token();
  enabled->send(foreign);
  enabled->send(stale, message_frame::subframe);
  enabled->send(stale, message_frame::main, "https://example.invalid/");
  CHECK(enabled->evaluations.size() == count);
  CHECK(child.window->reload().ok);
  enabled->send(stale);
  CHECK(enabled->evaluations.size() == count);
  enabled->send(enabled->envelope(1, "soui.asset.read", {{"path", "config.json"}}));
  enabled->await_asset();
  CHECK(enabled->last_response()["ok"] == true);
  // A separate root must not inherit the first child's resource directory.
  settings.resource_root = fixture.base / "resources-other";
  auto other = root.create_window(settings);
  CHECK(other.status.ok);
  auto* separate = latest;
  separate->send(separate->envelope(1, "soui.asset.read", {{"path", "config.json"}}));
  separate->await_asset();
  CHECK(separate->last_response()["ok"] == false);
  CHECK(child.window->close().ok);
  const auto closed_count = enabled->evaluations.size();
  enabled->send(enabled->envelope(2, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(enabled->evaluations.size() == closed_count);
}

void asynchronous_asset_contract(files& fixture) {
  auto settings = fixture.settings();
  settings.enable_asset_read = true;
  application app(settings);
  CHECK(app.initialize().ok);
  auto* native = latest;
  // Results waiting in the native UI queue retain their admission slots.
  for (unsigned id = 1; id <= 16; ++id)
    native->send(native->envelope(id, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(native->evaluations.empty());
  native->send(native->envelope(17, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(native->last_response()["error"]["code"] == "asset_queue_full");
  CHECK(native->wait_tasks(16));
  const auto before = native->evaluations.size();
  CHECK(app.reload().ok);
  native->flush(); // Already-posted old generation results must not be delivered.
  CHECK(native->evaluations.size() == before);
  native->send(native->envelope(1, "soui.asset.read", {{"path", "config.json"}}));
  native->await_asset();
  CHECK(native->last_response()["ok"] == true);
  CHECK(native->last_response()["id"] == 1);
  native->send(native->envelope(2, "soui.asset.read", {{"path", "config.json"}}));
  CHECK(native->wait_tasks());
  const auto count = native->evaluations.size();
  CHECK(app.close().ok);
  native->flush();
  CHECK(native->evaluations.size() == count);
}

void path_and_json_contract(files& fixture) {
  const auto base = "http://127.0.0.1:43210/" + std::string(64, 'a') + "/";
  const auto resource = detail::resource_path_uri(base, "folder space/中文.html");
  CHECK(detail::is_resource_uri(resource));
  CHECK(detail::same_document_uri(resource + "#fragment", resource));
  CHECK(!detail::same_document_uri(resource + "?query=1", resource));
  CHECK(!detail::same_document_uri(resource, "http://127.0.0.1:43211/" + std::string(64, 'a') + "/folder%20space/%E4%B8%AD%E6%96%87.html"));
  CHECK(!detail::same_document_uri(resource, detail::resource_path_uri(
      "http://127.0.0.1:43210/" + std::string(64, 'b') + "/", "folder space/中文.html")));
  for (const auto& origin : {"http://localhost:43210/", "http://127.0.0.1:0/",
      "http://127.0.0.1:65536/", "http://127.0.0.1:043210/",
      "http://127.0.0.1:43210@evil.invalid/", "https://127.0.0.1:43210/",
      "http://127.0.0.1.evil.invalid:43210/"})
    CHECK(!detail::is_resource_uri(std::string(origin) + std::string(64, 'a') + "/index.html"));
  CHECK(!detail::is_resource_uri("http://127.0.0.1:43210/short/index.html"));
  detail::document document;
  CHECK(detail::resolve_document(fixture.root, "index.html", document).ok);
  CHECK(document.uri.find("%23") != std::string::npos);
  CHECK(document.uri.find("%25") != std::string::npos);
  CHECK(detail::same_document_uri(document.uri + "#section", document.uri));
  CHECK(!detail::same_document_uri(document.uri + "?query=1", document.uri));
  CHECK(!detail::same_document_uri("https://example.invalid/", document.uri));
  CHECK(!detail::same_document_uri("file://server/index.html", document.uri));
  CHECK(!detail::same_document_uri("file:///index%00.html", document.uri));
  CHECK(!detail::same_document_uri("file:///index%GG.html", document.uri));
  CHECK(!detail::same_document_uri("file:///index%5c.html", document.uri));
  CHECK(!detail::resolve_document(fixture.root, "../outside.html", document).ok);
  CHECK(!detail::resolve_document(fixture.root, fixture.base / "outside.html", document).ok);
  CHECK(!detail::resolve_document(fixture.root, "missing.html", document).ok);
  CHECK(!detail::resolve_document(fixture.root, ".", document).ok);
  CHECK(!detail::resolve_document(fixture.root, "not-html.txt", document).ok);
  CHECK(detail::resolve_document(fixture.root, "upper.HTM", document).ok);
  std::error_code error;
  std::filesystem::create_symlink(fixture.base / "outside.html", fixture.root / "outside-link.html", error);
  if (!error) {
    CHECK(!detail::resolve_document(fixture.root, "outside-link.html", document).ok);
  } else {
    // Windows commonly needs Developer Mode or privilege for link creation.
    // Report the uncovered branch; never label it as a passing symlink test.
    std::cout << "NOT EXECUTED: symlink fixture creation: " << error.message() << '\n';
  }
  CHECK(detail::valid_name("app.echo-1"));
  CHECK(!detail::valid_name(""));
  CHECK(!detail::valid_name("bad name"));
  CHECK(!detail::valid_name(std::string(65, 'x')));
  CHECK(detail::parse_bounded_json("{\"x\":1}", 7).at("x") == 1);
  bool rejected = false;
  try { (void)detail::parse_bounded_json("{\"x\":1}", 6); } catch (...) { rejected = true; }
  CHECK(rejected);
  rejected = false;
  try { (void)detail::parse_bounded_json(std::string("\"\xC0\xAF\"", 4), 100); }
  catch (...) { rejected = true; }
  CHECK(rejected);
  rejected = false;
  try { (void)detail::parse_bounded_json(std::string(40, '[') + "0" + std::string(40, ']'), 200); }
  catch (...) { rejected = true; }
  CHECK(rejected);
}

void protocol_contract(files& fixture) {
  int invoked = 0, ready = 0, closed = 0;
  callbacks handlers;
  handlers.invoke = [&](std::string_view method, std::string_view params) {
    ++invoked;
    if (method == "throw") throw std::runtime_error("private handler detail");
    if (method == "invalid") return std::string("not JSON");
    if (method == "prototype") return std::string(R"({"__proto__":{"inherited":true},"text":"quote\"\\\u2028"})");
    return std::string(params);
  };
  handlers.ready = [&] { ++ready; };
  handlers.closed = [&] { ++closed; };
  application app(fixture.settings(), handlers);
  CHECK(app.initialize().ok);
  auto& native = *latest;
  CHECK(native.token().size() == 64);
  CHECK(!app.initialize().ok);
  CHECK(!app.emit("hello", "{}").ok);
  native.loaded();
  CHECK(ready == 1);

  auto valid = native.envelope(1, "echo", {{"text", "hello"}});
  native.send(valid, message_frame::subframe);
  native.send(valid, message_frame::unknown);
  native.send(valid, message_frame::main, "https://example.invalid/");
  auto invalid = valid; invalid["token"] = "wrong"; native.send(invalid);
  invalid = valid; invalid["id"] = 0; native.send(invalid);
  invalid = valid; invalid["id"] = -1; native.send(invalid);
  invalid = valid; invalid["id"] = 1.5; native.send(invalid);
  invalid = valid; invalid["id"] = detail::maximum_request_id + 1; native.send(invalid);
  invalid = valid; invalid["method"] = "bad method"; native.send(invalid);
  invalid = valid; invalid["v"] = 2; native.send(invalid);
  invalid = valid; invalid["extra"] = true; native.send(invalid);
  invalid = valid; invalid.erase("params"); native.send(invalid);
  invalid = valid; invalid["params"] = std::string(65536, 'a'); native.send(invalid);
  CHECK(invoked == 0);
  native.send(valid);
  CHECK(invoked == 1);
  CHECK(native.last_response().at("result").at("text") == "hello");
  CHECK(native.last_response().at("token") == native.token());
  native.send(valid);
  CHECK(invoked == 1);

  native.metadata = false;
  native.send(native.envelope(2), message_frame::unknown);
  CHECK(invoked == 2);  // GTK explicitly lacks frame metadata; token still required.
  invalid = native.envelope(3); invalid["token"] = "wrong";
  native.send(invalid, message_frame::unknown);
  CHECK(invoked == 2);
  native.send(native.envelope(3, "throw"));
  CHECK(native.last_response().at("ok") == false);
  CHECK(native.last_response().at("error").at("code") == "handler_failed");
  native.send(native.envelope(4, "invalid"));
  CHECK(native.last_response().at("ok") == false);
  native.send(native.envelope(5, "prototype"));
  CHECK(native.last_response().at("result").contains("__proto__"));
  CHECK(app.emit("event.test", R"({"__proto__":{"x":1}})").ok);
  CHECK(native.last_response().at("value").contains("__proto__"));
  native.evaluation_status = result::failure("test_evaluation_failure", "Transport rejected evaluation.");
  CHECK(app.emit("event.test", "{}").code == "test_evaluation_failure");
  CHECK(!app.emit("bad event", "{}").ok);
  CHECK(!app.emit("event", "not JSON").ok);
  CHECK(app.set_title("title").code == "test_unsupported");
  CHECK(app.resize(600, 400).code == "test_unsupported");
  CHECK(!app.resize(0, 400).ok);
  CHECK(app.show(true).code == "test_unsupported");
  CHECK(app.focus().code == "test_unsupported");
  CHECK(app.clipboard_read().status.code == "test_unsupported");
  CHECK(app.clipboard_write("text").code == "test_unsupported");
  CHECK(app.choose_directory().status.code == "test_unsupported");
  CHECK(!app.get_capabilities().clipboard_read);
  CHECK(app.close().ok && closed == 1);
  CHECK(app.close().ok && closed == 1);
  native.send(native.envelope(6));
  CHECK(!app.emit("event", "{}").ok);
  CHECK(!app.dispatch([] {}).ok);
}

void navigation_contract(files& fixture) {
  application* current = nullptr;
  int invoked = 0, closed = 0;
  callbacks handlers;
  handlers.invoke = [&](std::string_view method, std::string_view) {
    ++invoked;
    if (method == "navigate") CHECK(current->navigate("second.html").ok);
    if (method == "close") CHECK(current->close().ok);
    return std::string("{}");
  };
  handlers.closed = [&] { ++closed; };
  application app(fixture.settings(), handlers); current = &app;
  CHECK(app.initialize().ok);
  auto& native = *latest;
  const auto old_uri = native.uri;
  const auto old_token = native.token();
  const auto stale = native.envelope(2);
  native.send(native.envelope(1));
  CHECK(invoked == 1);
  CHECK(app.reload().ok);
  CHECK(native.token() != old_token);
  native.send(stale, message_frame::main, old_uri);
  CHECK(invoked == 1);
  native.send(native.envelope(1));
  CHECK(invoked == 2);
  const auto reload_token = native.token();
  const auto reload_callback = native.events.reload;
  reload_callback();
  CHECK(native.token() != reload_token);
  native.send(native.envelope(1));
  CHECK(invoked == 3);
  auto evaluation_count = native.evaluations.size();
  native.send(native.envelope(2, "navigate"));
  CHECK(invoked == 4);
  CHECK(native.evaluations.size() == evaluation_count);
  CHECK(native.uri != old_uri);
  native.send(native.envelope(1), message_frame::main, old_uri);
  CHECK(invoked == 4);
  native.send(native.envelope(1));
  CHECK(invoked == 5);
  evaluation_count = native.evaluations.size();
  native.send(native.envelope(2, "close"));
  CHECK(invoked == 6 && closed == 1);
  CHECK(native.evaluations.size() == evaluation_count);
}

void callback_and_thread_contract(files& fixture) {
  application* current = nullptr;
  bool in_callback = false, reentered = false;
  int invoked = 0, dispatched = 0, ready = 0, closed = 0;
  const auto owner = std::this_thread::get_id();
  callbacks handlers;
  handlers.invoke = [&](std::string_view, std::string_view) {
    if (in_callback) reentered = true;
    in_callback = true;
    if (++invoked == 1) {
      latest->send(latest->envelope(2));
      latest->loaded();
      CHECK(current->dispatch([&] {
        reentered = reentered || in_callback;
        CHECK(std::this_thread::get_id() == owner);
        ++dispatched;
      }).ok);
      latest->flush();  // Model a nested platform modal event loop.
    }
    in_callback = false;
    return std::string("{}");
  };
  handlers.ready = [&] { reentered = reentered || in_callback; ++ready; };
  handlers.closed = [&] { reentered = reentered || in_callback; ++closed; };
  application app(fixture.settings(), handlers); current = &app;
  CHECK(app.initialize().ok);
  auto& native = *latest;
  native.send(native.envelope(1));
  CHECK(invoked == 2);
  CHECK(ready == 1);
  native.flush();
  CHECK(dispatched == 1);
  CHECK(!reentered);
  result wrong_thread, submitted;
  std::thread worker([&] {
    wrong_thread = app.set_title("wrong thread");
    submitted = app.dispatch([&] {
      CHECK(std::this_thread::get_id() == owner); ++dispatched;
    });
  });
  worker.join();
  CHECK(!wrong_thread.ok && wrong_thread.code == "wrong_thread");
  CHECK(submitted.ok && dispatched == 1);
  native.flush();
  CHECK(dispatched == 2);
  CHECK(app.dispatch([&] { ++dispatched; }).ok);
  CHECK(app.close().ok);
  native.flush();
  CHECK(dispatched == 2 && closed == 1);
}

void initialization_contract(files& fixture) {
  auto invalid = fixture.settings(); invalid.width = 0;
  application invalid_app(invalid);
  CHECK(!invalid_app.initialize().ok);
  application app(fixture.settings());
  CHECK(app.initialize().ok);
  application second(fixture.settings());
  CHECK(second.initialize().code == "application_exists");
  latest->send(latest->envelope(1));
  CHECK(latest->last_response().at("error").at("code") == "method_not_registered");
  auto& native = *latest;
  native.load_status = result::failure("test_load_failed", "Transport refused navigation.");
  CHECK(app.navigate("second.html").code == "test_load_failed");
  const auto before = native.evaluations.size();
  native.send(native.envelope(2));
  CHECK(native.evaluations.size() == before);
  CHECK(!app.emit("event", "{}").ok);
}

std::shared_ptr<application> add_window(application& owner, const options& settings,
                                      callbacks handlers = {}) {
  auto created = owner.create_window(settings, std::move(handlers));
  CHECK(created.status.ok && created.window);
  if (!created.status.ok || !created.window)
    throw std::runtime_error("Cannot create managed test window: " + created.status.code);
  return std::move(created.window);
}

bool has_window(const application& owner, window_id id) {
  const auto ids = owner.window_ids();
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void multiwindow_identity_contract(files& fixture) {
  int root_calls = 0, child_calls = 0;
  std::vector<std::pair<window_id, window_event>> root_events, child_events;
  callbacks root_handlers;
  root_handlers.invoke = [&](std::string_view, std::string_view) { ++root_calls; return std::string("{}"); };
  root_handlers.lifecycle = [&](window_id id, window_event event) { root_events.emplace_back(id, event); };
  application root(fixture.settings(), root_handlers);
  CHECK(root.initialize().ok);
  auto& root_native = *latest;
  const auto root_counts = root_native.counts;
  CHECK(root.id() != 0);
  CHECK(root.window_ids().size() == 1 && has_window(root, root.id()));
  callbacks child_handlers;
  child_handlers.invoke = [&](std::string_view, std::string_view) { ++child_calls; return std::string("{}"); };
  child_handlers.lifecycle = [&](window_id id, window_event event) { child_events.emplace_back(id, event); };
  auto child = add_window(root, fixture.settings(), child_handlers);
  auto& child_native = *latest;
  CHECK(root.id() != child->id());
  CHECK(child->window_ids().size() == 2 && has_window(*child, root.id()));
  CHECK(root_native.uri == child_native.uri);
  CHECK(root_native.token() != child_native.token());
  root_native.loaded(); child_native.loaded();
  CHECK(root_events.size() == 1 && root_events[0].first == root.id() &&
        root_events[0].second == window_event::ready);
  CHECK(child_events.size() == 1 && child_events[0].first == child->id() &&
        child_events[0].second == window_event::ready);

  root_native.send(root_native.envelope(1));
  child_native.send(root_native.envelope(1));  // Same URI must not imply the same authority.
  root_native.send(child_native.envelope(1));
  CHECK(root_calls == 1 && child_calls == 0);
  child_native.send(child_native.envelope(1));
  root_native.send(root_native.envelope(1));
  child_native.send(child_native.envelope(1));
  CHECK(root_calls == 1 && child_calls == 1);
  root_native.send(root_native.envelope(2));
  child_native.send(child_native.envelope(2));
  CHECK(root_calls == 2 && child_calls == 2);
  const auto root_token = root_native.token();
  const auto old_child_request = child_native.envelope(3);
  CHECK(child->reload().ok);
  CHECK(root_native.token() == root_token);
  child_native.send(old_child_request);
  child_native.send(child_native.envelope(1));
  root_native.send(root_native.envelope(3));
  CHECK(root_calls == 3 && child_calls == 3);
  CHECK(!child->run().ok);
  const auto old_child_id = child->id();
  CHECK(child->close().ok);
  CHECK(root_native.active.load() && !child_native.active.load());
  CHECK(root_counts->loop_quits == 0);
  CHECK(root.window_ids().size() == 1 && !has_window(root, old_child_id));
  CHECK(child_events.size() == 2 && child_events.back().first == old_child_id &&
        child_events.back().second == window_event::closed);
  child_native.send(child_native.envelope(2));
  CHECK(child_calls == 3);
  auto replacement = add_window(root, fixture.settings());
  CHECK(replacement->id() != old_child_id && replacement->id() != root.id());
  CHECK(root.close().ok);
  CHECK(root_counts->loop_quits == 0);
  CHECK(root.window_ids().size() == 1 && has_window(root, replacement->id()));
  CHECK(root_events.size() == 2 && root_events.back().second == window_event::closed);
  CHECK(replacement->dispatch([replacement] { CHECK(replacement->close().ok); }).ok);
  CHECK(root.run().ok);  // The loop owner may have a closed native window.
  CHECK(root_counts->runs == 1 && root_counts->loop_quits == 1);
  CHECK(root.window_ids().empty());
  CHECK(!root.run().ok);
}

void group_lifetime_contract(files& fixture) {
  window_id prior_id = 0;
  {
    application root(fixture.settings());
    CHECK(root.initialize().ok);
    auto& root_native = *latest;
    const auto root_counts = root_native.counts;
    auto child = add_window(root, fixture.settings());
    auto& child_native = *latest;
    auto grandchild = add_window(*child, fixture.settings());
    auto& grandchild_native = *latest;
    CHECK(root.window_ids().size() == 3);
    CHECK(root.close().ok);
    CHECK(root_counts->loop_quits == 0);
    auto after_root_close = add_window(root, fixture.settings());
    CHECK(root.window_ids().size() == 3);
    CHECK(child->quit().ok);
    CHECK(root.window_ids().empty());
    CHECK(!child_native.active.load() && !grandchild_native.active.load());
    CHECK(root_counts->loop_quits == 1);
    CHECK(!after_root_close->dispatch([] {}).ok);
    CHECK(!root.create_window(fixture.settings()).status.ok);
    prior_id = after_root_close->id();
  }
  std::shared_ptr<application> orphan;
  std::shared_ptr<native_counts> root_counts, orphan_counts;
  {
    auto root = std::make_unique<application>(fixture.settings());
    CHECK(root->initialize().ok);
    CHECK(root->id() > prior_id);
    root_counts = latest->counts;
    orphan = add_window(*root, fixture.settings());
    orphan_counts = latest->counts;
    prior_id = orphan->id();
    root.reset();  // An outstanding child handle cannot keep the group alive.
  }
  CHECK(root_counts->closed == 1 && orphan_counts->closed == 1);
  CHECK(root_counts->loop_quits == 1);
  CHECK(orphan->window_ids().empty());
  CHECK(!orphan->dispatch([] {}).ok);
  CHECK(!orphan->navigate("index.html").ok);
  {
    application replacement(fixture.settings());
    CHECK(replacement.initialize().ok);  // Old invalid handles do not retain the process slot.
    CHECK(replacement.id() > prior_id);
    auto child = add_window(replacement, fixture.settings());
    auto request_quit = latest->events.quit_requested;
    CHECK(static_cast<bool>(request_quit));
    request_quit();
    CHECK(replacement.window_ids().empty() && !child->dispatch([] {}).ok);
  }
}

void quit_during_window_creation_contract(files& fixture) {
  application root(fixture.settings());
  CHECK(root.initialize().ok);
  const auto root_counts = latest->counts;
  std::shared_ptr<native_counts> child_counts;
  during_next_create = [&] {
    // Native creation can pump a nested event loop (COM/AppKit/GTK). The
    // initializing child is not a live window until native create returns.
    child_counts = latest->counts;
    CHECK(root.quit().ok);
    CHECK(root.window_ids().empty());
  };
  const auto child = root.create_window(fixture.settings());
  CHECK(!child.status.ok && child.status.code == "closed" && !child.window);
  CHECK(child_counts && child_counts->closed == 1);
  CHECK(root_counts->loop_quits == 1);
  CHECK(root.window_ids().empty());
  CHECK(!root.run().ok);
}

void window_capacity_contract(files& fixture) {
  application root(fixture.settings());
  CHECK(root.initialize().ok);
  std::vector<std::shared_ptr<application>> children;
  for (int i = 0; i < 31; ++i) children.push_back(add_window(root, fixture.settings()));
  CHECK(root.window_ids().size() == 32);
  const auto rejected = root.create_window(fixture.settings());
  CHECK(!rejected.status.ok && !rejected.window);
  CHECK(root.window_ids().size() == 32);
  const auto previous = children.back()->id();
  CHECK(children.back()->close().ok);
  children.push_back(add_window(root, fixture.settings()));
  CHECK(children.back()->id() > previous && root.window_ids().size() == 32);
  auto invalid = fixture.settings(); invalid.entry = "../outside.html";
  CHECK(children.back()->close().ok);
  CHECK(!root.create_window(invalid).status.ok);
  CHECK(root.window_ids().size() == 31);
  window_result nested;
  during_next_create = [&] {
    // The outer native window already reserves the final slot even though it
    // has not finished create(). Nested UI pumping must not overbook it.
    nested = root.create_window(fixture.settings());
  };
  auto final_window = root.create_window(fixture.settings());
  CHECK(final_window.status.ok && final_window.window);
  CHECK(!nested.status.ok && nested.status.code == "window_limit" && !nested.window);
  CHECK(root.window_ids().size() == 32);
}

void cross_window_callbacks_contract(files& fixture) {
  bool in_root = false, reentered = false;
  int child_calls = 0, child_ready = 0, child_commands = 0, child_work = 0, child_closed = 0;
  fake_backend* child_native = nullptr;
  std::shared_ptr<application> child;
  callbacks root_handlers;
  root_handlers.invoke = [&](std::string_view method, std::string_view) {
    in_root = true;
    child_native->send(child_native->envelope(1));
    child_native->loaded();
    child_native->command("action");
    CHECK(child->dispatch([&] { reentered |= in_root; ++child_work; }).ok);
    child_native->flush();
    if (method == "close-child") CHECK(child->close().ok);
    CHECK(child_calls == 0 && child_ready == 0 && child_commands == 0 && child_work == 0 && child_closed == 0);
    in_root = false;
    return std::string("{}");
  };
  application root(fixture.settings(), root_handlers);
  CHECK(root.initialize().ok);
  auto& root_native = *latest;
  callbacks child_handlers;
  child_handlers.invoke = [&](std::string_view, std::string_view) {
    reentered |= in_root; ++child_calls; return std::string("{}");
  };
  child_handlers.ready = [&] { reentered |= in_root; ++child_ready; };
  child_handlers.command = [&](std::string_view) { reentered |= in_root; ++child_commands; };
  child_handlers.closed = [&] { reentered |= in_root; ++child_closed; };
  child = add_window(root, fixture.settings(), child_handlers);
  child_native = latest;
  child_native->menu_status = result::success();
  CHECK(child->popup_menu({{"action", "Action"}}).ok);
  root_native.send(root_native.envelope(1));
  CHECK(child_calls == 1 && child_ready == 1 && child_commands == 1 && child_work == 1);
  CHECK(!reentered);
  CHECK(child->close().ok && child_closed == 1);

  // With close occurring inside the other window's modal callback, queued
  // commands, work and requests are discarded; the close notification remains.
  child_calls = child_ready = child_commands = child_work = child_closed = 0;
  child = add_window(root, fixture.settings(), child_handlers);
  child_native = latest;
  child_native->menu_status = result::success();
  CHECK(child->popup_menu({{"action", "Action"}}).ok);
  root_native.send(root_native.envelope(2, "close-child"));
  CHECK(child_calls == 0 && child_ready == 0 && child_commands == 0 && child_work == 0);
  CHECK(child_closed == 1 && !reentered);
}

void native_actions_contract(files& fixture) {
  std::vector<std::string> root_commands, child_commands;
  callbacks root_handlers;
  root_handlers.command = [&](std::string_view id) { root_commands.emplace_back(id); };
  application root(fixture.settings(), root_handlers);
  CHECK(root.initialize().ok);
  auto& native = *latest;
  CHECK(root.popup_menu({{"unsupported", "Unavailable"}}).code == "test_unsupported");
  native.command("unsupported");
  CHECK(root_commands.empty());
  native.menu_status = native.tray_status = native.clear_tray_status = result::success();
  CHECK(root.get_capabilities().popup_menu && root.get_capabilities().tray);
  CHECK(root.popup_menu({}).ok);
  CHECK(native.popup_items.empty());
  const auto unchanged = native.counts->popup_calls;
  CHECK(!root.popup_menu({{"bad id", "Label"}}).ok);
  CHECK(!root.popup_menu({{"valid", ""}}).ok);
  CHECK(!root.popup_menu({{"same", "One"}, {"same", "Two"}}).ok);
  CHECK(!root.popup_menu({{"separator", "", true, false, true}}).ok);
  CHECK(!root.popup_menu({{"", "separator", true, false, true}}).ok);
  CHECK(!root.popup_menu({{"valid", std::string(4097, 'x')}}).ok);
  CHECK(!root.popup_menu({{"valid", std::string("bad\0label", 9)}}).ok);
  CHECK(!root.popup_menu({{"valid", std::string("\xC0\xAF", 2)}}).ok);
  std::vector<menu_item> maximum;
  for (int i = 0; i < 65; ++i) maximum.push_back({"item" + std::to_string(i), "Item"});
  CHECK(!root.popup_menu(maximum).ok);
  CHECK(native.counts->popup_calls == unchanged);
  maximum.pop_back();
  CHECK(root.popup_menu(maximum).ok);
  CHECK(native.popup_items.size() == 64);
  const std::string maximum_id(64, 'x');
  CHECK(root.popup_menu({{maximum_id, std::string(4096, 'x')}}).ok);
  CHECK(!root.popup_menu({{maximum_id + "x", "Label"}}).ok);
  CHECK(root.popup_menu({{"enabled", "Enabled"}, {"disabled", "Disabled", false},
                         {"", "", true, false, true}}).ok);
  native.command("enabled"); native.command("disabled"); native.command("unknown");
  CHECK(root_commands == std::vector<std::string>{"enabled"});
  CHECK(native.evaluations.empty());  // No command is silently sent to the web page.

  callbacks child_handlers;
  child_handlers.command = [&](std::string_view id) { child_commands.emplace_back(id); };
  auto child = add_window(root, fixture.settings(), child_handlers);
  auto& child_native = *latest;
  child_native.menu_status = result::success();
  CHECK(child->popup_menu({{"enabled", "Same ID, separate owner"}}).ok);
  child_native.command("enabled");
  CHECK(root_commands.size() == 1 && child_commands.size() == 1);
  native.command("enabled");
  CHECK(root_commands.size() == 2 && child_commands.size() == 1);

  tray_options tray;
  tray.label = std::string(32, 'S');
  tray.tooltip = std::string(4096, 't');
  tray.menu = {{"tray-only", "Tray"}, {"disabled-tray", "Disabled", false}, {"enabled", "Shared command"}};
  CHECK(root.set_tray(tray).ok);
  native.command("tray-only"); native.command("disabled-tray"); native.command("enabled");
  CHECK(root_commands.size() == 4 && root_commands[2] == "tray-only");
  const auto tray_calls = native.counts->tray_calls;
  auto invalid = tray; invalid.label += "S"; CHECK(!root.set_tray(invalid).ok);
  invalid = tray; invalid.label.clear(); CHECK(!root.set_tray(invalid).ok);
  invalid = tray; invalid.tooltip += "t"; CHECK(!root.set_tray(invalid).ok);
  invalid = tray; invalid.menu = {{"same", "One"}, {"same", "Two"}};
  CHECK(!root.set_tray(invalid).ok);
  CHECK(native.counts->tray_calls == tray_calls);
  CHECK(root.clear_tray().ok);
  native.command("tray-only"); native.command("enabled");
  CHECK(root_commands.size() == 5 && root_commands.back() == "enabled");
  CHECK(root.set_tray(tray_options{}).ok);  // A tray may have no menu.
  native.menu_status = result::failure("test_menu_failure", "The platform rejected the menu.");
  CHECK(root.popup_menu({{"failed-registration", "No action"}}).code == "test_menu_failure");
  native.command("failed-registration");
  CHECK(root_commands.size() == 5);
  CHECK(child->close().ok);
  child_native.command("enabled");
  CHECK(child_commands.size() == 1);
}

void hierarchical_native_actions_contract(files& fixture) {
  std::vector<std::string> commands;
  callbacks handlers;
  handlers.command = [&](std::string_view id) { commands.emplace_back(id); };
  application app(fixture.settings(), handlers);
  CHECK(app.initialize().ok);
  auto& native = *latest;
  native.menu_status = native.tray_status = result::success();
  const auto check_tree = [&](const std::vector<menu_item>& tree, bool valid) {
    const auto popup_calls = native.counts->popup_calls;
    const auto tray_calls = native.counts->tray_calls;
    const auto popup = app.popup_menu(tree);
    tray_options tray; tray.menu = tree;
    const auto status = app.set_tray(tray);
    CHECK(popup.ok == valid && status.ok == valid);
    if (!valid) CHECK(popup.code == "invalid_menu" && status.code == "invalid_menu");
    CHECK(native.counts->popup_calls == popup_calls + (valid ? 1 : 0));
    CHECK(native.counts->tray_calls == tray_calls + (valid ? 1 : 0));
  };
  check_tree({}, true);

  menu_item depth_four{"deep", "Depth four", true, true};
  for (int i = 0; i < 3; ++i)
    depth_four = {"", "Level", true, false, false, {std::move(depth_four)}};
  check_tree({depth_four}, true);
  CHECK(native.popup_items.front().children.front().children.front().children.front().checked);
  check_tree({{"", "Depth five", true, false, false, {depth_four}}}, false);

  // The limit counts submenu parents and separators, not only leaf commands.
  menu_item full{"", "Full tree", true, false, false, {{"", "", true, false, true}}};
  for (int i = 0; i < 62; ++i) full.children.push_back({"item" + std::to_string(i), "Item"});
  check_tree({full}, true);  // 1 parent + 1 separator + 62 commands = 64.
  check_tree({full, {"overflow", "Overflow"}}, false);
  full.children.pop_back();
  check_tree({full, {"last", "Last"}}, true);
  full.children.back().id = "last";
  check_tree({full, {"last", "Last"}}, false);

  const menu_item leaf{"action", "Action"};
  check_tree({{"", "One", true, false, false, {leaf}},
              {"", "Two", false, false, false, {leaf}}}, false);
  check_tree({{"parent-id", "Parent", true, false, false, {leaf}}}, false);
  check_tree({{"", "Parent", true, true, false, {leaf}}}, false);
  check_tree({{"", "", true, false, false, {leaf}}}, false);
  check_tree({{"", std::string(4097, 'x'), true, false, false, {leaf}}}, false);
  check_tree({{"", std::string("bad\0label", 9), true, false, false, {leaf}}}, false);
  check_tree({{"", std::string("\xC0\xAF", 2), true, false, false, {leaf}}}, false);
  check_tree({{"", "", true, false, true, {leaf}}}, false);
  check_tree({{"", "Parent", true, false, false, {{"", "No leaf ID"}}}}, false);
  check_tree({{"", "Parent", true, false, false, {{"bad id", "Bad leaf ID"}}}}, false);

  const std::vector<menu_item> enabled_tree = {
    {"", "Disabled root", false, false, false, {
      {"", "Enabled child", true, false, false, {{"root-blocked", "Blocked"}}}}},
    {"", "Enabled root", true, false, false, {
      {"", "Disabled child", false, false, false, {{"child-blocked", "Blocked"}}},
      {"leaf-blocked", "Disabled leaf", false}, {"allowed", "Checked leaf", true, true}}}
  };
  CHECK(app.set_tray(tray_options{}).ok);
  CHECK(app.popup_menu(enabled_tree).ok);
  const auto issue_commands = [&] {
    for (const char* id : {"root-blocked", "child-blocked", "leaf-blocked", "", "unknown", "allowed"})
      native.command(id);
  };
  issue_commands();
  CHECK(commands == std::vector<std::string>{"allowed"});
  CHECK(app.popup_menu({}).ok);
  tray_options tray; tray.menu = enabled_tree;
  CHECK(app.set_tray(tray).ok);
  issue_commands();
  CHECK(commands == (std::vector<std::string>{"allowed", "allowed"}));
  CHECK(app.set_tray(tray_options{}).ok);
  native.command("allowed");
  CHECK(commands.size() == 2);  // Empty roots revoke every previous command.
}

void deferred_native_action_revision_contract(files& fixture) {
  std::shared_ptr<application> child;
  fake_backend* child_native = nullptr;
  int commands = 0;
  const auto nested = [](std::string label) {
    return std::vector<menu_item>{{"", "Actions", true, false, false,
      {{"shared", std::move(label)}}}};
  };
  callbacks root_handlers;
  root_handlers.invoke = [&](std::string_view method, std::string_view) {
    child_native->command("shared");
    CHECK(commands == 0); // Deferred until the other window's callback returns.
    if (method == "replace") {
      CHECK(child->popup_menu(nested("Replacement action")).ok);
    } else if (method == "remove-tray") {
      CHECK(child->clear_tray().ok);
    } else {
      child_native->menu_status = result::failure("test_menu_failure", "Menu rejected.");
      CHECK(!child->popup_menu(nested("Failed action")).ok);
    }
    return std::string("{}");
  };
  application root(fixture.settings(), root_handlers);
  CHECK(root.initialize().ok);
  auto& root_native = *latest;
  callbacks handlers;
  handlers.command = [&](std::string_view) { ++commands; };
  child = add_window(root, fixture.settings(), handlers);
  child_native = latest;
  child_native->menu_status = child_native->tray_status =
      child_native->clear_tray_status = result::success();
  CHECK(child->popup_menu(nested("Original action")).ok);
  root_native.send(root_native.envelope(1, "replace"));
  CHECK(commands == 0);
  tray_options tray;
  tray.menu = nested("Tray action");
  CHECK(child->set_tray(tray).ok);
  root_native.send(root_native.envelope(2, "remove-tray"));
  CHECK(commands == 0); // Same ID still exists in popup but old tray event is stale.
  root_native.send(root_native.envelope(3, "failed-replacement"));
  CHECK(commands == 0); // Failed reconfiguration also cancels deferred actions.
  child_native->command("shared");
  CHECK(commands == 1); // The retained popup still accepts newly generated events.
  // A native popup can pump a tray update before returning failure. The
  // unrelated tray revision must not prevent restoring the prior popup IDs.
  child_native->while_popup = [&] { CHECK(child->set_tray(tray_options{}).ok); };
  CHECK(!child->popup_menu({{"rejected", "Rejected popup"}}).ok);
  child_native->while_popup = {};
  child_native->command("rejected");
  CHECK(commands == 1);
  child_native->command("shared");
  CHECK(commands == 2);
}
void close_approval_contract(files& fixture) {
  {
    application app(fixture.settings());
    CHECK(app.request_close().code == "closed");
    CHECK(app.request_quit().code == "closed");
    CHECK(app.initialize().ok);
    auto* native = latest;
    std::thread worker([&] {
      CHECK(app.request_close().code == "wrong_thread");
      CHECK(app.request_quit().code == "wrong_thread");
    });
    worker.join();
    native->events.close_requested(); // Missing handler defaults to allow.
    CHECK(native->counts->closed == 1);
    CHECK(app.request_close().code == "closed");
  }
  {
    application* app = nullptr;
    int approvals = 0, errors = 0;
    bool allow = false, throwing = false;
    callbacks handlers;
    handlers.close_requested = [&](window_id id) {
      ++approvals;
      CHECK(id == app->id());
      CHECK(app->request_close().code == "close_busy");
      CHECK(app->request_quit().code == "close_busy");
      if (throwing) throw std::runtime_error("approval failure");
      return allow;
    };
    handlers.error = [&](const result& error) {
      ++errors;
      CHECK(error.code == "close_callback_failed");
      CHECK(app->request_close().code == "close_busy");
      CHECK(app->request_quit().code == "close_busy");
    };
    application root(fixture.settings(), handlers); app = &root;
    CHECK(root.initialize().ok);
    auto* native = latest;
    CHECK(root.request_close().code == "close_cancelled");
    native->events.close_requested();
    CHECK(approvals == 2 && errors == 0 && native->active.load());
    throwing = true;
    CHECK(root.request_close().code == "close_cancelled");
    CHECK(approvals == 3 && errors == 1 && native->active.load());
    throwing = false; allow = true;
    CHECK(root.request_close().ok);
    CHECK(approvals == 4 && native->counts->closed == 1);
  }
  {
    application* app = nullptr;
    bool in_callback = false;
    int approvals = 0;
    callbacks handlers;
    handlers.invoke = [&](std::string_view, std::string_view) {
      in_callback = true;
      CHECK(app->request_close().ok);
      CHECK(app->request_close().ok); // Pending requests coalesce.
      CHECK(approvals == 0);
      in_callback = false;
      return std::string("{}");
    };
    handlers.close_requested = [&](window_id) { CHECK(!in_callback); ++approvals; return true; };
    application root(fixture.settings(), handlers); app = &root;
    CHECK(root.initialize().ok);
    auto* native = latest;
    native->send(native->envelope(1));
    CHECK(approvals == 1 && native->counts->closed == 1);
  }
  {
    application* app = nullptr;
    int approvals = 0;
    callbacks handlers;
    handlers.invoke = [&](std::string_view, std::string_view) {
      CHECK(app->request_close().ok);
      CHECK(app->close().ok); // Force-close discards queued approval.
      return std::string("{}");
    };
    handlers.close_requested = [&](window_id) { ++approvals; return false; };
    application root(fixture.settings(), handlers); app = &root;
    CHECK(root.initialize().ok);
    latest->send(latest->envelope(1));
    CHECK(approvals == 0);
  }
}

void global_shortcut_contract(files& fixture) {
  using namespace shortcut_modifiers;
  int delivered = 0;
  window_id delivered_window = 0;
  std::string delivered_id;
  callbacks handlers;
  handlers.global_shortcut = [&](window_id owner, std::string_view id) {
    ++delivered; delivered_window = owner; delivered_id = id;
  };
  application app(fixture.settings(), handlers);
  const global_shortcut chord{shortcut_key::f1, control};
  CHECK(app.register_global_shortcut("open", chord).code == "closed");
  CHECK(app.initialize().ok);
  auto& native = *latest;
  CHECK(app.get_capabilities().global_shortcuts);
  for (const auto& id : {std::string{}, std::string("has space"), std::string(129, 'x')}) {
    CHECK(app.register_global_shortcut(id, chord).code == "invalid_shortcut_id");
    CHECK(app.unregister_global_shortcut(id).code == "invalid_shortcut_id");
  }
  for (const auto invalid : {global_shortcut{}, global_shortcut{shortcut_key::f2, shift},
      global_shortcut{static_cast<shortcut_key>(-1), control},
      global_shortcut{static_cast<shortcut_key>(1000), control},
      global_shortcut{shortcut_key::f3, 16U | control}})
    CHECK(app.register_global_shortcut("bad", invalid).code == "invalid_shortcut");
  CHECK(native.shortcut_registrations.empty());
  native.shortcut_register_status = unsupported();
  native.while_shortcut_register = [&](std::uint64_t token) { native.shortcut(token); native.flush(); };
  CHECK(app.register_global_shortcut("open", chord).code == "test_unsupported");
  const auto failed_token = native.shortcut_registrations.back().first;
  CHECK(delivered == 0);
  native.shortcut_register_status = result::success();
  native.while_shortcut_register = [&](std::uint64_t token) {
    CHECK(app.unregister_global_shortcut("cancelled").code == "shortcut_cleanup_pending");
    native.shortcut(token); native.flush();
  };
  CHECK(app.register_global_shortcut("cancelled", chord).code == "shortcut_cancelled");
  const auto cancelled_token = native.shortcut_registrations.back().first;
  CHECK(native.shortcut_unregistrations.back() == cancelled_token && delivered == 0);
  native.shortcut(cancelled_token); native.flush();
  CHECK(delivered == 0);
  native.while_shortcut_register = [&](std::uint64_t token) { native.shortcut(token); native.flush(); };
  CHECK(app.register_global_shortcut("open", chord).ok);
  const auto first_token = native.shortcut_registrations.back().first;
  CHECK(first_token > failed_token && delivered == 0);
  CHECK(app.register_global_shortcut("open", {shortcut_key::f2, alt}).code == "duplicate_shortcut_id");
  CHECK(app.register_global_shortcut("same-chord", chord).code == "duplicate_shortcut");
#if defined(__APPLE__)
  CHECK(app.register_global_shortcut("primary", {shortcut_key::f1, primary}).ok);
  CHECK(native.shortcut_registrations.back().second.modifiers == primary);
  CHECK(app.unregister_global_shortcut("primary").ok);
#else
  CHECK(app.register_global_shortcut("primary", {shortcut_key::f1, primary | control}).code == "duplicate_shortcut");
  CHECK(app.register_global_shortcut("primary", {shortcut_key::f2, primary | shift}).ok);
  CHECK(native.shortcut_registrations.back().second.modifiers == (control | shift));
  CHECK(app.unregister_global_shortcut("primary").ok);
#endif
  native.shortcut(failed_token);
  native.shortcut(first_token);
  CHECK(delivered == 0); // No inline shortcut callback from the native event.
  native.flush();
  CHECK(delivered == 1 && delivered_window == app.id() && delivered_id == "open");
  CHECK(native.evaluations.empty()); // No automatic renderer event.
  native.shortcut(first_token);
  CHECK(app.navigate("second.html").ok);
  native.menu_status = result::success();
  CHECK(app.popup_menu({{"menu", "Menu"}}).ok);
  native.flush();
  CHECK(delivered == 2); // Neither page nor menu generations revoke registrations.
  native.shortcut(first_token);
  native.shortcut_unregister_status = unsupported();
  native.while_shortcut_unregister = [&](std::uint64_t token) { native.shortcut(token); native.flush(); };
  CHECK(app.unregister_global_shortcut("open").code == "test_unsupported");
  CHECK(delivered == 2); // Revoked before even a failed native unregister.
  CHECK(app.register_global_shortcut("open", chord).code == "duplicate_shortcut_id");
  CHECK(app.register_global_shortcut("other", chord).code == "duplicate_shortcut");
  native.shortcut(first_token); native.flush();
  CHECK(delivered == 2);
  native.shortcut_unregister_status = result::success();
  CHECK(app.unregister_global_shortcut("open").ok);
  const auto cleanup_count = native.shortcut_unregistrations.size();
  CHECK(app.unregister_global_shortcut("open").ok);
  CHECK(native.shortcut_unregistrations.size() == cleanup_count);
  CHECK(app.register_global_shortcut("open", chord).ok);
  const auto next_token = native.shortcut_registrations.back().first;
  CHECK(next_token > first_token);
  native.shortcut(first_token); native.shortcut(next_token); native.flush();
  CHECK(delivered == 3);
  std::thread worker([&] {
    CHECK(app.register_global_shortcut("worker", chord).code == "wrong_thread");
    CHECK(app.unregister_global_shortcut("open").code == "wrong_thread");
  });
  worker.join();
  native.shortcut(next_token);
  native.shortcut_unregister_status = unsupported();
  CHECK(app.close().code == "test_unsupported");
  CHECK(native.counts->closed == 1 && delivered == 3);
  native.shortcut(next_token); native.flush();
  CHECK(delivered == 3 && app.register_global_shortcut("closed", chord).code == "closed");
  native.shortcut_unregister_status = result::success();
  CHECK(app.close().ok); // Common retries; only this fake guarantees cleanup after close.
}

void global_shortcut_capacity_contract(files& fixture) {
  using namespace shortcut_modifiers;
  int delivered = 0;
  callbacks handlers;
  handlers.global_shortcut = [&](window_id, std::string_view) { ++delivered; };
  application app(fixture.settings(), handlers);
  CHECK(app.initialize().ok);
  auto& native = *latest;
  for (unsigned i = 0; i < 32; ++i)
    CHECK(app.register_global_shortcut("slot" + std::to_string(i),
      {static_cast<shortcut_key>(i % 11), i < 11 ? control : i < 22 ? alt : control | alt}).ok);
  const global_shortcut extra{shortcut_key::delete_key, alt};
  CHECK(app.register_global_shortcut("extra", extra).code == "shortcut_limit");
  native.shortcut_unregister_status = unsupported();
  CHECK(app.unregister_global_shortcut("slot0").code == "test_unsupported");
  CHECK(app.register_global_shortcut("extra", extra).code == "shortcut_limit");
  native.shortcut_unregister_status = result::success();
  CHECK(app.unregister_global_shortcut("slot0").ok);
  native.shortcut_register_status = result::failure("shortcut_cleanup_pending", "Native rollback failed.");
  CHECK(app.register_global_shortcut("extra", extra).code == "shortcut_cleanup_pending");
  CHECK(app.register_global_shortcut("extra", extra).code == "duplicate_shortcut_id");
  CHECK(app.register_global_shortcut("same-extra", extra).code == "duplicate_shortcut");
  CHECK(app.register_global_shortcut("beyond-limit", {shortcut_key::home, alt}).code == "shortcut_limit");
  const auto pending_token = native.shortcut_registrations.back().first;
  native.shortcut(pending_token); native.flush();
  CHECK(delivered == 0);
  CHECK(app.unregister_global_shortcut("extra").ok);
  CHECK(native.shortcut_unregistrations.back() == pending_token);
  native.shortcut_register_status = result::success();
  CHECK(app.register_global_shortcut("extra", extra).ok);
  const auto token = native.shortcut_registrations.back().first;
  CHECK(app.dispatch([&] {
    // A nested native pump cannot enter another business callback. Its 300
    // notifications must fit the common 128-event queue, not the work queue.
    for (unsigned i = 0; i < 300; ++i) native.shortcut(token);
    native.flush();
    CHECK(delivered == 0);
  }).ok);
  CHECK(delivered == 0);
  native.flush();
  CHECK(delivered == 128); // Independent bounded native shortcut notification queue.
  native.shortcut(token); native.flush();
  CHECK(delivered == 129);
  native.dispatch_status = result::failure("E_WEB_PROCESS", "Fake renderer terminated.");
  CHECK(app.dispatch([] {}).code == "E_WEB_PROCESS");
  native.shortcut(token); native.flush();
  CHECK(delivered == 130); // Native shortcut delivery does not require a renderer.
  const auto before = native.shortcut_unregistrations.size();
  native.while_shortcut_unregister = [&](std::uint64_t selected) {
    // Cleanup revokes every registration, not just the one currently being removed.
    native.shortcut(token); native.shortcut(selected); native.flush();
    CHECK(delivered == 130);
  };
  CHECK(app.close().ok);
  CHECK(native.shortcut_unregistrations.size() == before + 32);
}

void global_shortcut_reentry_contract(files& fixture) {
  using namespace shortcut_modifiers;
  application* app = nullptr;
  std::shared_ptr<application> child;
  fake_backend* root_native = nullptr;
  fake_backend* child_native = nullptr;
  std::uint64_t root_token = 0, child_token = 0;
  bool inside_invoke = false, inside_shortcut = false;
  int root_calls = 0, child_calls = 0, errors = 0;
  callbacks handlers;
  handlers.invoke = [&](std::string_view, std::string_view) {
    inside_invoke = true;
    root_native->shortcut(root_token); child_native->shortcut(child_token);
    root_native->flush(); child_native->flush();
    CHECK(root_calls == 0 && child_calls == 0);
    inside_invoke = false;
    return "{}";
  };
  handlers.global_shortcut = [&](window_id owner, std::string_view id) {
    CHECK(!inside_invoke && !inside_shortcut && owner == app->id() && id == "open");
    ++root_calls;
    inside_shortcut = true;
    child_native->shortcut(child_token); child_native->flush();
    CHECK(child_calls == 0);
    // The queued old token must not acquire the reused logical ID.
    root_native->shortcut(root_token);
    CHECK(app->unregister_global_shortcut("open").ok);
    CHECK(app->register_global_shortcut("open", {shortcut_key::f1, control}).ok);
    CHECK(id == "open"); // Callback argument survives registration-vector mutation.
    inside_shortcut = false;
  };
  handlers.error = [&](const result& error) { CHECK(error.code == "callback_failed"); ++errors; };
  application root(fixture.settings(), handlers); app = &root;
  CHECK(root.initialize().ok); root_native = latest;
  callbacks child_handlers;
  child_handlers.global_shortcut = [&](window_id owner, std::string_view id) {
    CHECK(!inside_invoke && !inside_shortcut && owner == child->id() && id == "open");
    ++child_calls;
  };
  child = add_window(root, fixture.settings(), child_handlers); child_native = latest;
  CHECK(root.register_global_shortcut("open", {shortcut_key::f1, control}).ok);
  CHECK(child->register_global_shortcut("open", {shortcut_key::f1, alt}).ok);
  root_token = root_native->shortcut_registrations.back().first;
  child_token = child_native->shortcut_registrations.back().first;
  CHECK(child_token > root_token);
  root_native->shortcut(child_token); child_native->shortcut(root_token);
  root_native->flush(); child_native->flush();
  CHECK(root_calls == 0 && child_calls == 0);
  root_native->send(root_native->envelope(1));
  root_native->flush(); child_native->flush();
  CHECK(root_calls == 1 && child_calls == 2 && errors == 0);
  const auto replacement = root_native->shortcut_registrations.back().first;
  CHECK(replacement > child_token);
  root_native->shortcut(replacement);
  root_native->while_shortcut_unregister = [&](std::uint64_t token) {
    root_native->shortcut(token); root_native->flush();
  };
  CHECK(root.close().ok);
  CHECK(root_calls == 1 && child_calls == 2 && child_native->active.load());
  child_native->shortcut(child_token); child_native->flush();
  CHECK(child_calls == 3);
}

void global_shortcut_close_contract(files& fixture) {
  {
    application* app = nullptr;
    bool in_callback = false;
    int calls = 0, errors = 0, closes = 0;
    callbacks handlers;
    handlers.global_shortcut = [&](window_id, std::string_view) {
      CHECK(!in_callback);
      if (++calls == 1) throw std::runtime_error("business callback failure");
      in_callback = true;
      CHECK(app->close().ok);
      CHECK(closes == 0);
      in_callback = false;
    };
    handlers.error = [&](const result& error) { CHECK(error.code == "callback_failed"); ++errors; };
    handlers.closed = [&] { CHECK(!in_callback); ++closes; };
    application root(fixture.settings(), handlers); app = &root;
    CHECK(root.initialize().ok);
    auto& native = *latest;
    CHECK(root.register_global_shortcut("close", {shortcut_key::end, shortcut_modifiers::alt}).ok);
    const auto token = native.shortcut_registrations.back().first;
    native.while_shortcut_unregister = [&](std::uint64_t selected) { native.shortcut(selected); native.flush(); };
    for (int i = 0; i < 3; ++i) native.shortcut(token);
    native.flush();
    CHECK(calls == 2 && errors == 1 && closes == 1 && native.counts->closed == 1);
  }
  {
    int delivered = 0, closes = 0;
    bool in_registration = false;
    callbacks handlers;
    handlers.global_shortcut = [&](window_id, std::string_view) { ++delivered; };
    handlers.closed = [&] { CHECK(!in_registration); ++closes; };
    application root(fixture.settings(), handlers);
    CHECK(root.initialize().ok);
    auto& native = *latest;
    native.while_shortcut_register = [&](std::uint64_t token) {
      in_registration = true;
      native.shortcut(token);
      CHECK(root.close().code == "shortcut_cleanup_pending"); // Close while this native call is still in progress.
      CHECK(closes == 0);
      in_registration = false;
    };
    CHECK(root.register_global_shortcut("close", {shortcut_key::end, shortcut_modifiers::alt}).code == "closed");
    CHECK(delivered == 0 && closes == 1 && native.shortcut_unregistrations.size() == 1);
  }
  {
    application root(fixture.settings());
    CHECK(root.initialize().ok);
    auto& native = *latest;
    CHECK(root.register_global_shortcut("first", {shortcut_key::home, shortcut_modifiers::alt}).ok);
    CHECK(root.register_global_shortcut("second", {shortcut_key::end, shortcut_modifiers::alt}).ok);
    bool nested = false;
    native.while_shortcut_unregister = [&](std::uint64_t) {
      if (nested) return;
      nested = true;
      CHECK(root.close().code == "shortcut_cleanup_pending");
    };
    CHECK(root.unregister_global_shortcut("first").ok);
    CHECK(native.shortcut_unregistrations.size() == 2 && native.counts->closed == 1);
  }
}

void quit_approval_contract(files& fixture) {
  {
    int approvals = 0;
    bool allow = false;
    application root(fixture.settings());
    CHECK(root.initialize().ok);
    callbacks child_handlers;
    child_handlers.close_requested = [&](window_id) { ++approvals; return allow; };
    auto child = add_window(root, fixture.settings(), child_handlers);
    CHECK(root.close().ok);
    CHECK(root.request_quit().code == "close_cancelled");
    CHECK(approvals == 1 && root.window_ids().size() == 1);
    allow = true;
    CHECK(root.request_quit().ok);
    CHECK(approvals == 2 && root.window_ids().empty());
  }
  application* app = nullptr;
  std::shared_ptr<application> child, created;
  fake_backend* root_native = nullptr;
  bool veto = true, create_during_approval = false, force_close = false;
  bool inside_invoke = false;
  int root_approvals = 0, child_approvals = 0;
  callbacks handlers;
  handlers.close_requested = [&](window_id) {
    CHECK(!inside_invoke);
    ++root_approvals;
    CHECK(child->request_close().code == (has_window(*app, child->id()) ?
      "close_busy" : "closed"));
    CHECK(app->request_quit().code == "close_busy");
    if (create_during_approval) created = add_window(*app, fixture.settings());
    if (force_close) CHECK(child->close().ok);
    return true;
  };
  handlers.invoke = [&](std::string_view, std::string_view) {
    inside_invoke = true;
    const int before = root_approvals;
    CHECK(child->request_close().ok);
    CHECK(app->request_quit().ok);
    CHECK(app->request_quit().ok);
    CHECK(root_approvals == before);
    inside_invoke = false;
    return std::string("{}");
  };
  application root(fixture.settings(), handlers); app = &root;
  CHECK(root.initialize().ok); root_native = latest;
  callbacks child_handlers;
  child_handlers.close_requested = [&](window_id) {
    CHECK(!inside_invoke);
    CHECK(root_native->counts->closed == 0); // Approve all before closing any.
    ++child_approvals;
    return !veto;
  };
  child = add_window(root, fixture.settings(), child_handlers);
  auto child_counts = latest->counts;
  CHECK(root.request_quit().code == "close_cancelled");
  CHECK(root.window_ids().size() == 2 && root_approvals == 1 && child_approvals == 1);
  root_native->events.quit_requested();
  CHECK(root_native->counts->closed == 0 && child_counts->closed == 0);
  root_native->send(root_native->envelope(1));
  CHECK(root_approvals == 3 && child_approvals == 3 && root.window_ids().size() == 2);
  veto = false; create_during_approval = true;
  CHECK(root.request_quit().code == "close_cancelled");
  CHECK(root.window_ids().size() == 3 && child_counts->closed == 0);
  create_during_approval = false; force_close = true;
  CHECK(root.request_quit().code == "close_cancelled");
  CHECK(child_counts->closed == 1 && root_native->counts->closed == 0);
  force_close = false;
  CHECK(root.request_quit().ok);
  CHECK(root.window_ids().empty() && root_native->counts->closed == 1);
}
}  // namespace

namespace soui::desktop::detail {
std::unique_ptr<backend> make_backend() {
  auto value = std::make_unique<fake_backend>(); latest = value.get(); return value;
}
}  // namespace soui::desktop::detail

int main(int argc, char** argv) {
  if ((argc == 2 || argc == 3) && std::string(argv[1]) == "--preload") {
    std::cout << soui::desktop::detail::make_preload(argc == 3 ? argv[2] : "test-navigation-token"); return 0;
  }
  try {
    files fixture;
    global_shortcut_contract(fixture);
    global_shortcut_capacity_contract(fixture);
    global_shortcut_reentry_contract(fixture);
    global_shortcut_close_contract(fixture);
    if (argc == 2 && std::string(argv[1]) == "--global-shortcuts") {
      std::cout << "Desktop global-shortcut fake contract: " << checks << " checks, "
                << failures << " failures. No native shortcut registration verification.\n";
      return failures ? 1 : 0;
    }
    desktop_services_contract(fixture);
    desktop_service_reentry_contract(fixture);
    window_state_contract(fixture);
    asset_bridge_contract(fixture);
    asynchronous_asset_contract(fixture);
    path_and_json_contract(fixture);
    protocol_contract(fixture);
    navigation_contract(fixture);
    callback_and_thread_contract(fixture);
    initialization_contract(fixture);
    multiwindow_identity_contract(fixture);
    group_lifetime_contract(fixture);
    quit_during_window_creation_contract(fixture);
    window_capacity_contract(fixture);
    cross_window_callbacks_contract(fixture);
    native_actions_contract(fixture);
    hierarchical_native_actions_contract(fixture);
    deferred_native_action_revision_contract(fixture);
    close_approval_contract(fixture);
    quit_approval_contract(fixture);
  } catch (const std::exception& error) {
    ++failures; std::cerr << "Unexpected test exception: " << error.what() << '\n';
  }
  std::cout << "Desktop common/fake-backend contract: " << checks << " checks, "
            << failures << " failures. No native GUI/system capability verification.\n";
  return failures ? 1 : 0;
}
