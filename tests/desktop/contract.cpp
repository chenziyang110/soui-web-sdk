#include <soui/desktop/app.h>
#include "../../sdk/src/desktop/backend.h"
#include "../../sdk/src/desktop/protocol.h"

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
  result load_status{result::success()};
  result menu_status{unsupported()}, tray_status{unsupported()}, clear_tray_status{unsupported()};
  std::vector<menu_item> popup_items;
  tray_options tray_settings;
  std::function<void()> while_popup;
  std::mutex mutex;
  std::condition_variable task_ready;
  std::deque<std::function<void()>> tasks;
  int loads{0};

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
  result set_title(std::string_view) override { return unsupported(); }
  result resize(int, int) override { return unsupported(); }
  result show(bool) override { return unsupported(); }
  result focus() override { return unsupported(); }
  value_result clipboard_read() override { return {unsupported(), {}}; }
  result clipboard_write(std::string_view) override { return unsupported(); }
  value_result choose_directory() override { return {unsupported(), {}}; }
  result popup_menu(const std::vector<menu_item>& items) override {
    ++counts->popup_calls; popup_items = items;
    if (while_popup) while_popup();
    return menu_status;
  }
  result set_tray(const tray_options& settings) override {
    ++counts->tray_calls; tray_settings = settings; return tray_status;
  }
  result clear_tray() override { ++counts->clear_tray_calls; return clear_tray_status; }
  capabilities get_capabilities() const override {
    return {"test-fake", false, false, false, false, false, menu_status.ok, tray_status.ok};
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
  const auto unchanged = native.counts->popup_calls;
  CHECK(!root.popup_menu({}).ok);
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

void deferred_native_action_revision_contract(files& fixture) {
  std::shared_ptr<application> child;
  fake_backend* child_native = nullptr;
  int commands = 0;
  callbacks root_handlers;
  root_handlers.invoke = [&](std::string_view method, std::string_view) {
    child_native->command("shared");
    CHECK(commands == 0); // Deferred until the other window's callback returns.
    if (method == "replace") {
      CHECK(child->popup_menu({{"shared", "Replacement action"}}).ok);
    } else if (method == "remove-tray") {
      CHECK(child->clear_tray().ok);
    } else {
      child_native->menu_status = result::failure("test_menu_failure", "Menu rejected.");
      CHECK(!child->popup_menu({{"shared", "Failed action"}}).ok);
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
  CHECK(child->popup_menu({{"shared", "Original action"}}).ok);
  root_native.send(root_native.envelope(1, "replace"));
  CHECK(commands == 0);
  tray_options tray;
  tray.menu = {{"shared", "Tray action"}};
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
    deferred_native_action_revision_contract(fixture);
  } catch (const std::exception& error) {
    ++failures; std::cerr << "Unexpected test exception: " << error.what() << '\n';
  }
  std::cout << "Desktop common/fake-backend contract: " << checks << " checks, "
            << failures << " failures. No native GUI/system capability verification.\n";
  return failures ? 1 : 0;
}
