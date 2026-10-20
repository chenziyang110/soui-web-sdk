#include <soui/desktop/app.h>
#include "../../sdk/src/desktop/backend.h"
#include "../../sdk/src/desktop/protocol.h"

#include <atomic>
#include <chrono>
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

// A deliberately simple transport fake. No platform API is called. Tests may
// trigger callbacks reentrantly to model a modal native message loop.
class fake_backend final : public detail::backend {
 public:
  detail::backend_events events;
  std::atomic<bool> active{false};
  bool metadata{true};
  std::string uri, preload;
  std::vector<std::string> evaluations;
  result evaluation_status{result::success()};
  result load_status{result::success()};
  std::mutex mutex;
  std::deque<std::function<void()>> tasks;
  int loads{0};

  result create(const options&, detail::backend_events callbacks) override {
    events = std::move(callbacks); active.store(true); return result::success();
  }
  result load(std::string selected, std::string script) override {
    ++loads; uri = std::move(selected); preload = std::move(script); return load_status;
  }
  result run() override { flush(); return result::success(); }
  result close() override {
    if (!active.exchange(false)) return result::success();
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
    tasks.push_back(std::move(work)); return result::success();
  }
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
  capabilities get_capabilities() const override { return {"test-fake", false, false, false, false, false}; }
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
}  // namespace

namespace soui::desktop::detail {
std::unique_ptr<backend> make_backend() {
  auto value = std::make_unique<fake_backend>(); latest = value.get(); return value;
}
}  // namespace soui::desktop::detail

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--preload") {
    std::cout << soui::desktop::detail::make_preload("test-navigation-token"); return 0;
  }
  try {
    files fixture;
    path_and_json_contract(fixture);
    protocol_contract(fixture);
    navigation_contract(fixture);
    callback_and_thread_contract(fixture);
    initialization_contract(fixture);
  } catch (const std::exception& error) {
    ++failures; std::cerr << "Unexpected test exception: " << error.what() << '\n';
  }
  std::cout << "Desktop common/fake-backend contract: " << checks << " checks, "
            << failures << " failures. No native GUI/system capability verification.\n";
  return failures ? 1 : 0;
}
