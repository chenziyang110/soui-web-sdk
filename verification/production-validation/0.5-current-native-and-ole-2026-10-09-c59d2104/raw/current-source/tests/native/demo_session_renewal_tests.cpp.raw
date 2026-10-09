#include "../../demos/workbench/integration/demo_bridge_adapter.h"
#include <json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace soui::demo {
// This seam seeds ownership records, never opens a picker, creates a HWND,
// initializes a media decoder, or starts media encoding.
struct demo_bridge_test_access {
  struct resources { std::string media, preview, workload, file; };
  static resources seed(demo_bridge_adapter& app, const nlohmann::json& auth,
                        const std::filesystem::path& path, bool running = true) {
    soui::runtime::scan_request_context context{
        auth.at("sessionId"), auth.at("origin"), {}, {}};
    context.authorization = app.permissions_.resolve(context.session_id, context.origin,
        auth.at("token"), "media.convert").scope;
    if (!context.authorization) throw std::runtime_error("seed authorization missing");
    resources ids;
    ids.media = app.tasks_.start("seed-media").task_id;
    ids.preview = app.tasks_.start("seed-preview").task_id;
    ids.workload = app.tasks_.start("seed-workload").task_id;
    ids.file = "seed-file";
    auto media = std::make_shared<demo_bridge_adapter::media_job>();
    media->context = context; media->task_id = ids.media;
    app.media_jobs_.emplace(ids.media, media);
    auto preview = std::make_shared<demo_bridge_adapter::media_preview_job>();
    preview->context = context; preview->task_id = ids.preview;
    app.media_preview_jobs_.emplace(ids.preview, preview);
    auto workload = std::make_shared<demo_bridge_adapter::workload_job>();
    workload->context = context; workload->task_id = ids.workload; workload->kind = "cad";
    app.workload_jobs_.emplace(ids.workload, workload);
    app.media_files_.emplace(ids.file, demo_bridge_adapter::media_file_binding{context, path, "a.txt", 5});
    if (running) for (const auto& id : {ids.media, ids.preview, ids.workload}) (void)app.tasks_.mark_running(id);
    return ids;
  }
  static bool file_owned(demo_bridge_adapter& app, const nlohmann::json& auth, const std::string& id) {
    const auto scope = app.permissions_.resolve(auth.at("sessionId"), auth.at("origin"),
                                               auth.at("token"), "media.convert").scope;
    std::lock_guard<std::mutex> lock(app.media_mutex_);
    const auto found = app.media_files_.find(id);
    return found != app.media_files_.end() && scope && scope->active() &&
           found->second.context.authorization == scope && found->second.context.token.empty() &&
           found->second.context.session_id == auth.at("sessionId") &&
           found->second.context.origin == auth.at("origin");
  }
  static void emit(demo_bridge_adapter& app, const resources& ids, unsigned progress) {
    app.emit_media_event(*app.media_jobs_.at(ids.media), "running", progress, "seed progress");
    app.emit_media_preview_event(*app.media_preview_jobs_.at(ids.preview), "running", progress, "seed progress");
    app.emit_workload_event(*app.workload_jobs_.at(ids.workload), "running", progress, "seed progress");
  }
  static bool empty(demo_bridge_adapter& app) {
    return app.media_jobs_.empty() && app.media_preview_jobs_.empty() && app.workload_jobs_.empty() &&
           app.media_files_.empty() && app.subscriptions_.empty();
  }
  static void launch_workload(demo_bridge_adapter& app, const resources& ids) {
    std::lock_guard<std::mutex> lock(app.workload_mutex_);
    auto* job = app.workload_jobs_.at(ids.workload).get();
    job->worker = std::thread([&app, job] { app.run_workload_job(job); });
  }
  static std::shared_ptr<const soui::runtime::authorization_scope> scanner_gate(
      demo_bridge_adapter& app, const nlohmann::json& auth, const std::function<void()>& gate,
      const std::function<void()>& returned) {
    const auto scope = app.permissions_.resolve(auth.at("sessionId"), auth.at("origin"),
                                                auth.at("token"), "filesystem.scan").scope;
    app.scanner_.set_event_sink([&app, gate, returned](std::string_view text) {
      const auto event = nlohmann::json::parse(text);
      if (event.at("payload").at("state") != "running") return;
      gate();
      // This copied worker callback intentionally touches the same state as the
      // production wrapper after destruction has begun. Teardown must join it
      // while the callback mutex and event-sink member are still alive.
      {
        std::lock_guard<std::mutex> lock(app.subscription_mutex_);
        const auto detached = !app.event_sink_;
        if (!detached) throw std::runtime_error("destructor did not detach its sink");
      }
      returned();
    });
    return scope;
  }
  static bool gate_handler(demo_bridge_adapter& app, const std::string& alias,
      const std::string& method, const std::function<void()>& gate) {
    const auto original = app.registry_.handler_for(method);
    const auto capability = app.registry_.capability_for(method);
    if (!original || !capability) return false;
    auto callback = *original;
    return app.registry_.register_contextual_method(alias, *capability,
        [gate, callback](const soui::runtime::bridge_request& request) {
          // The transport has resolved the real bearer before this barrier.
          gate();
          return callback(request);
        });
  }
};
}  // namespace soui::demo

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;
using adapter = soui::demo::demo_bridge_adapter;
using access = soui::demo::demo_bridge_test_access;
std::atomic<std::int64_t> elapsed_ms{0};

soui::runtime::permission_store::clock_type::time_point now() {
  return soui::runtime::permission_store::clock_type::time_point{std::chrono::milliseconds(elapsed_ms.load())};
}
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
json bootstrap(adapter& app) {
  const auto response = json::parse(app.handle_json(json{{"requestId", "bootstrap"}, {"sessionId", ""},
      {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(response.at("ok").get<bool>(), "Demo bootstrap rejected");
  require(response.at("result").at("lease").at("generation") == 1, "Demo bootstrap did not opt into renewal");
  return response.at("result");
}
json call(adapter& app, const json& auth, const std::string& method, json params) {
  static std::atomic<unsigned> sequence{0};
  return json::parse(app.handle_json(json{{"requestId", "renewal-demo-" + std::to_string(++sequence)},
      {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")}, {"token", auth.at("token")},
      {"method", method}, {"params", std::move(params)}}.dump()));
}
void success(const json& response, const char* message) {
  require(response.value("ok", false), message);
}
void rejected(const json& response, const char* message) {
  require(!response.value("ok", true), message);
}
struct fixture {
  std::filesystem::path path;
  fixture() {
    path = std::filesystem::temp_directory_path() /
        ("soui-demo-renewal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(path), "fixture directory creation failed");
    std::ofstream(path / "a.txt") << "alpha";
    std::ofstream(path / "b.txt") << "beta";
  }
  ~fixture() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void test_demo_resources_survive_rotation() {
  elapsed_ms = 0;
  fixture files;
  std::mutex event_mutex;
  std::vector<json> events;
  adapter app(&now);
  auto auth = bootstrap(app);
  const auto old_auth = auth;
  app.set_event_sink([&](std::string_view text) {
    std::lock_guard<std::mutex> lock(event_mutex);
    events.push_back(json::parse(text));
  });
  const auto ids = access::seed(app, auth, files.path / "a.txt");
  soui::runtime::scan_request_context context{auth.at("sessionId"), auth.at("origin"), auth.at("token")};
  const auto root = app.register_root(context, files.path);
  require(!root.empty(), "Demo root registration failed");
  const auto scan = call(app, auth, "demo.scan.start",
      {{"rootToken", root}, {"filter", ""}, {"operationId", "original-scan"}});
  success(scan, "Demo scan start rejected");
  const auto scan_id = scan.at("result").at("taskId").get<std::string>();
  soui::runtime::scan_response first;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  do {
    first = app.read_scan_page(context, scan_id, {}, 1);
    if (first.entries.size() == 1 && first.has_more) break;
    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);
  require(first.entries.size() == 1 && first.has_more && !first.next_cursor.empty(),
          "Demo scan did not create retained cursor");
  std::vector<std::string> subscriptions;
  for (const auto& id : {ids.media, ids.preview, ids.workload, scan_id}) {
    const auto response = call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
    success(response, "Demo original subscription failed");
    subscriptions.push_back(response.at("result").at("subscriptionId"));
  }
  std::uint64_t sequence = 0;
  for (unsigned round = 1; round <= 3; ++round) {
    elapsed_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
    const auto renewal = call(app, auth, "bridge.session.renew", {{"operationId", "demo-renew-" + std::to_string(round)}});
    success(renewal, "Demo renewal strict params were changed by legacy augmentation");
    const auto next = renewal.at("result");
    require(next.at("sessionId") == auth.at("sessionId") && next.at("origin") == auth.at("origin") &&
            next.at("token") != auth.at("token") && next.at("lease").at("generation") == round + 1,
            "Demo renewal changed session identity or failed to rotate");
    auth = next;
    context.token = auth.at("token");
    for (const auto& id : {ids.media, ids.preview, ids.workload}) {
      const auto query = call(app, auth, "task.query", {{"taskId", id}});
      success(query, "renewed Demo credentials lost a legacy task");
      require(query.at("result").at("taskId") == id, "Demo renewal replaced a task ID");
    }
    require(access::file_owned(app, auth, ids.file), "renewed Demo credentials lost original media file");
    const auto page = app.read_scan_page(context, scan_id, first.next_cursor, 1);
    require(page.ok() && page.entries.size() == 1 && page.entries.front().relative_path == "b.txt",
            "renewal lost original root, scan result or cursor");
    const auto duplicate = call(app, auth, "demo.scan.start",
        {{"rootToken", root}, {"filter", ""}, {"operationId", "original-scan"}});
    success(duplicate, "renewed token could not reuse original root");
    require(duplicate.at("result").at("taskId") == scan_id, "renewal recreated old scan");
    std::size_t before;
    { std::lock_guard<std::mutex> lock(event_mutex); before = events.size(); }
    access::emit(app, ids, round);
    {
      std::lock_guard<std::mutex> lock(event_mutex);
      require(events.size() == before + 3, "pre-renewal Demo subscriptions lost events");
      for (std::size_t index = before; index < events.size(); ++index) {
        require(events[index].at("sequence").get<std::uint64_t>() > sequence, "Demo event sequence reset at renewal");
        sequence = events[index].at("sequence");
      }
    }
  }
  const auto bad_shape = call(app, auth, "bridge.session.renew", {{"operationId", "bad"}, {"token", "forged"}});
  rejected(bad_shape, "Demo renewal accepted unknown credential fields");
  rejected(call(app, old_auth, "task.query", {{"taskId", ids.media}}), "expired old Demo bearer retained access");
  auto stranger = auth;
  stranger["token"] = app.issue_session(auth.at("sessionId"), auth.at("origin"),
      {"filesystem.read", "task.start", "media.convert", "desktop.workload", "bridge.session.renew"});
  for (const auto& id : {ids.media, ids.preview, ids.workload, scan_id}) {
    rejected(call(app, stranger, "task.query", {{"taskId", id}}), "same-session independent grant stole Demo task");
    rejected(call(app, stranger, "task.cancel", {{"taskId", id}}), "same-session independent grant cancelled Demo task");
    rejected(call(app, stranger, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}}),
             "same-session independent grant subscribed to Demo task");
  }
  require(!access::file_owned(app, stranger, ids.file), "same-session independent grant stole media file");
  for (const auto& subscription : subscriptions) {
    rejected(call(app, stranger, "task.unsubscribe", {{"subscriptionId", subscription}}),
             "same-session independent grant removed original subscription");
    success(call(app, auth, "task.unsubscribe", {{"subscriptionId", subscription}}),
            "renewed Demo bearer could not remove original subscription");
  }
  for (const auto& id : {ids.media, ids.preview, ids.workload})
    success(call(app, auth, "task.cancel", {{"taskId", id}}), "renewed Demo bearer could not cancel original task");
  for (const auto& id : {ids.media, ids.preview, ids.workload})
    success(call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}}),
            "Demo expiry test subscription failed");
  elapsed_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(30min).count());
  std::size_t before_expired_emit;
  { std::lock_guard<std::mutex> lock(event_mutex); before_expired_emit = events.size(); }
  access::emit(app, ids, 99);
  {
    std::lock_guard<std::mutex> lock(event_mutex);
    require(events.size() == before_expired_emit, "expired Demo grant emitted newly dispatched progress");
  }
  require(!access::file_owned(app, auth, ids.file), "expired Demo grant retained media-file authority");
  app.shutdown_session(auth.at("sessionId"));
  require(access::empty(app), "Demo close did not clear all owned resources");
  rejected(call(app, auth, "bridge.session.renew", {{"operationId", "after-close"}}),
           "Demo close allowed a stale credential to renew");
}

class request_gate {
 public:
  void block() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    if (!changed_.wait_for(lock, 5s, [&] { return released_; }))
      throw std::runtime_error("request gate release timed out");
  }
  void wait_entered() {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [&] { return entered_; }), "request did not pass authorization before close");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex_); released_ = true; changed_.notify_all(); }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_{false}, released_{false};
};

void test_revoke_after_transport_before_admission() {
  for (const bool subscription : {false, true}) {
    elapsed_ms = 0;
    fixture files;
    request_gate gate;
    adapter app(&now);
    const auto auth = bootstrap(app);
    const auto ids = access::seed(app, auth, files.path / "a.txt");
    require(access::gate_handler(app, "test.after-authorization",
        subscription ? "task.subscribe" : "desktop.workload.start", [&] { gate.block(); }),
        "could not install bounded native test gate");
    const json params = subscription
        ? json{{"taskId", ids.media}, {"eventName", "task.progress"}}
        : json{{"kind", "cad"}, {"complexity", 1}, {"operationId", "late-work"}};
    auto result = std::async(std::launch::async, [&] {
      return call(app, auth, "test.after-authorization", params);
    });
    gate.wait_entered();
    app.shutdown_session(auth.at("sessionId"));
    gate.release();
    require(result.wait_for(5s) == std::future_status::ready, "late request did not finish");
    rejected(result.get(), "already-authorized request admitted new work after shutdown");
    require(access::empty(app), "late request repopulated resources after shutdown");
  }
}
void test_demo_subscription_capacity() {
  elapsed_ms = 0;
  fixture files;
  adapter app(&now);
  auto auth = bootstrap(app);
  const auto root = app.register_root({auth.at("sessionId"), auth.at("origin"), auth.at("token")}, files.path);
  const auto started = call(app, auth, "demo.scan.start",
      {{"rootToken", root}, {"filter", ""}, {"operationId", "capacity-scan"}});
  success(started, "capacity scan was not accepted");
  const auto id = started.at("result").at("taskId");
  std::vector<std::string> subscriptions;
  for (std::size_t i = 0; i < soui::runtime::task_service::max_subscriptions; ++i) {
    const auto response = call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
    success(response, "available Demo subscription capacity was rejected");
    subscriptions.push_back(response.at("result").at("subscriptionId"));
  }
  const auto full = call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
  require(!full.at("ok").get<bool>() && full.at("error").at("code") == "E_BUSY",
          "Demo subscription retention exceeded its hard limit");
  success(call(app, auth, "task.unsubscribe", {{"subscriptionId", subscriptions.front()}}),
          "Demo capacity was not released by unsubscribe");
  const auto twice = call(app, auth, "task.unsubscribe", {{"subscriptionId", subscriptions.front()}});
  require(!twice.at("ok").get<bool>() && twice.at("error").at("code") == "E_SUBSCRIPTION_NOT_FOUND",
          "repeated unsubscribe incorrectly released more capacity");
  const auto replacement = call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
  success(replacement, "freed Demo subscription capacity was not reusable");
  require(replacement.at("result").at("subscriptionId") != subscriptions.front(), "retired subscription ID was reused");
  const auto full_again = call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
  require(!full_again.at("ok").get<bool>() && full_again.at("error").at("code") == "E_BUSY",
          "duplicate unsubscribe inflated Demo capacity");
  app.shutdown_session(auth.at("sessionId"));
  require(access::empty(app), "Demo close retained full subscription table");
  auth = bootstrap(app);
  const auto new_root = app.register_root({auth.at("sessionId"), auth.at("origin"), auth.at("token")}, files.path);
  const auto restarted = call(app, auth, "demo.scan.start",
      {{"rootToken", new_root}, {"filter", ""}, {"operationId", "capacity-scan-replacement"}});
  success(restarted, "replacement session could not scan");
  success(call(app, auth, "task.subscribe",
      {{"taskId", restarted.at("result").at("taskId")}, {"eventName", "task.progress"}}),
      "replacement session did not recover subscription capacity after close");
}

void test_throwing_listener_does_not_stop_workload() {
  elapsed_ms = 0;
  fixture files;
  std::atomic<unsigned> callbacks{0};
  std::promise<void> terminal;
  auto completed = terminal.get_future();
  adapter app(&now);
  const auto auth = bootstrap(app);
  const auto ids = access::seed(app, auth, files.path / "a.txt", false);
  for (const auto& id : {ids.media, ids.preview, ids.workload})
    success(call(app, auth, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}}),
            "throwing-listener subscription setup failed");
  app.set_event_sink([&](std::string_view text) {
    ++callbacks;
    const auto event = json::parse(text);
    if (event.at("taskId") == ids.workload && event.at("payload").at("state") == "succeeded")
      terminal.set_value();
    throw std::runtime_error("deliberate external listener failure");
  });
  // Exercise all three Demo notification helpers, then the actual bounded CAD
  // worker. No media decoder, encoder, HWND, or modal dialog is invoked.
  access::emit(app, ids, 1);
  require(callbacks.load() == 3, "Demo listener exception escaped or stopped sibling events");
  access::launch_workload(app, ids);
  require(completed.wait_for(5s) == std::future_status::ready, "throwing listener stopped the actual workload worker");
  const auto result = call(app, auth, "task.query", {{"taskId", ids.workload}});
  success(result, "workload result was lost after listener failure");
  require(result.at("result").at("state") == "succeeded", "workload did not complete after listener failure");
  app.shutdown_session(auth.at("sessionId"));
  require(access::empty(app), "throwing listener prevented Demo cleanup");
}

void test_direct_session_destructor_joins_copied_scanner_callback() {
  elapsed_ms = 0;
  fixture files;
  request_gate gate;
  std::atomic<bool> callback_returned{false};
  auto app = std::make_unique<adapter>(&now);
  json auth{{"sessionId", "direct-native"}, {"origin", "app://direct-native"}};
  auth["token"] = app->issue_session(auth.at("sessionId"), auth.at("origin"), {"filesystem.read"});
  const auto scope = access::scanner_gate(*app, auth, [&] { gate.block(); },
                                          [&] { callback_returned.store(true); });
  const auto root = app->register_root({auth.at("sessionId"), auth.at("origin"), auth.at("token")}, files.path);
  success(call(*app, auth, "demo.scan.start",
      {{"rootToken", root}, {"filter", ""}, {"operationId", "direct-destructor"}}),
      "direct native session scan setup failed");
  gate.wait_entered();
  auto destroyed = std::async(std::launch::async, [&] { app.reset(); });
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (scope->active() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
  const bool revoked_before_join = !scope->active();
  const bool blocked_on_callback = destroyed.wait_for(0s) != std::future_status::ready;
  gate.release();
  require(destroyed.wait_for(5s) == std::future_status::ready, "Demo destructor did not join scanner callback");
  destroyed.get();
  require(revoked_before_join && blocked_on_callback && callback_returned.load(),
          "direct-session destructor did not revoke then safely join its copied scanner callback");
}

}  // namespace

int main() {
  try {
    test_demo_resources_survive_rotation();
    test_revoke_after_transport_before_admission();
    test_demo_subscription_capacity();
    test_throwing_listener_does_not_stop_workload();
    test_direct_session_destructor_joins_copied_scanner_callback();
    std::cout << "Demo renewal ownership and revoke-before-admission contracts passed; no GUI or media engine executed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
