#include <soui/desktop/app.h>
#include <json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
using json = nlohmann::json;
using soui::desktop::application;
namespace {
struct window_facts {
  unsigned ready{}, armed{}, requests{}, closed{}, reentrant_rejections{};
  bool retired_api_rejected{}, idempotent_close{}, callback_released{};
};
struct facts {
  std::array<window_facts, 3> windows;
  unsigned errors{};
  bool initialized{}, webview2{}, run{}, quit{}, veto_live{}, stale_counts_stable{};
  std::atomic<bool> timeout{false}, timeout_dispatch_failed{false};
  std::string failure;
};
class deadline {
  std::mutex mutex_;
  std::condition_variable changed_;
  bool finished_{};
  std::thread worker_;
 public:
  deadline(application& app, facts& f) : worker_([&, this] {
    std::unique_lock<std::mutex> lock(mutex_);
    if (changed_.wait_for(lock, std::chrono::seconds(30), [this] { return finished_; })) return;
    lock.unlock();
    f.timeout = true;
    if (!app.dispatch([&app] { (void)app.quit(); }).ok) f.timeout_dispatch_failed = true;
  }) {}
  ~deadline() {
    { std::lock_guard<std::mutex> lock(mutex_); finished_ = true; }
    changed_.notify_one();
    worker_.join();
  }
};
}
int main() {
  facts f;
  try {
    const auto resources = soui::desktop::application_resource_directory();
    if (!resources.status.ok) throw std::runtime_error("resource_root");
    soui::desktop::options settings;
    settings.resource_root = resources.value;
    settings.title = "SOUI Windows DOM close regression";
    settings.enable_resource_server = true;
    settings.enable_window_controls = true;
    settings.user_data_directory = resources.value.parent_path() / "regression-profile-root";
    application* root = nullptr;
    std::array<std::shared_ptr<application>, 2> children;
    std::array<std::weak_ptr<int>, 2> lifetime;
    std::array<unsigned, 2> retired_requests{};
    std::function<void(unsigned)> spawn;
    const auto fail = [&](const std::string& reason) {
      if (f.failure.empty()) f.failure = reason;
      if (root) (void)root->dispatch([&] { (void)root->quit(); });
    };
    const auto error = [&](const soui::desktop::result& result) {
      ++f.errors;
      fail(result.code);
    };
    spawn = [&](unsigned child_index) {
      const unsigned i = child_index + 1;
      auto marker = std::make_shared<int>(1);
      lifetime[child_index] = marker;
      auto child_settings = settings;
      child_settings.user_data_directory = resources.value.parent_path() /
        ("regression-profile-child-" + std::to_string(i));
      soui::desktop::callbacks events;
      events.ready = [&, i] { ++f.windows[i].ready; };
      events.error = error;
      events.invoke = [&, i](std::string_view method, std::string_view params) -> std::string {
        if (method == "fixture.start") {
          ++f.windows[i].armed;
          return R"({"mode":"approve"})";
        }
        if (method == "fixture.fail" && params.size() <= 4096) {
          fail("page_" + json::parse(params).value("reason", std::string("unknown")));
          return "null";
        }
        fail("unexpected_child_method");
        return "null";
      };
      events.close_requested = [&, i, child_index](soui::desktop::window_id id) {
        auto& w = f.windows[i];
        ++w.requests;
        if (!children[child_index]) { fail("child_handle_missing"); return false; }
        if (id != children[child_index]->id() || w.armed != 1)
          fail("child_identity_or_arm");
        // Reentrant proposals must reject as busy, not recurse or notify twice.
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
          const auto r = children[child_index]->request_close();
          if (!r.ok && r.code == "close_busy") ++w.reentrant_rejections;
        }
        return true;
      };
      events.closed = [&, i, child_index, marker] {
        ++f.windows[i].closed;
        // Destroy a public handle only after its own callback has unwound.
        if (!root->dispatch([&, i, child_index] {
          auto& w = f.windows[i];
          const auto a = children[child_index]->request_close();
          const auto b = children[child_index]->request_close();
          w.retired_api_rejected = !a.ok && !b.ok && a.code == "closed" && b.code == "closed";
          w.idempotent_close = children[child_index]->close().ok && children[child_index]->close().ok && w.closed == 1;
          retired_requests[child_index] = w.requests;
          children[child_index].reset();
          w.callback_released = lifetime[child_index].expired();
          if (child_index == 0) spawn(1);
          else {
            f.stale_counts_stable = f.windows[1].requests == retired_requests[0] &&
              f.windows[1].closed == 1 && f.windows[2].requests == retired_requests[1];
            if (!root->close().ok) fail("root_cleanup");
          }
        }).ok) fail("retire_dispatch");
      };
      const auto created = root->create_window(std::move(child_settings), std::move(events));
      if (!created.status.ok) { fail(created.status.code); return; }
      children[child_index] = created.window;
    };
    soui::desktop::callbacks events;
    events.ready = [&] { ++f.windows[0].ready; };
    events.closed = [&] { ++f.windows[0].closed; };
    events.error = error;
    events.close_requested = [&](soui::desktop::window_id id) {
      ++f.windows[0].requests;
      if (!root || id != root->id() || f.windows[0].armed != 1) fail("root_identity_or_arm");
      return false;
    };
    events.invoke = [&](std::string_view method, std::string_view params) -> std::string {
      if (method == "fixture.start") {
        ++f.windows[0].armed;
        return R"({"mode":"veto"})";
      }
      if (method == "fixture.state")
        return json{{"requests", f.windows[0].requests}, {"closed", f.windows[0].closed}}.dump();
      if (method == "fixture.fail" && params.size() <= 4096) {
        fail("page_" + json::parse(params).value("reason", std::string("unknown")));
        return "null";
      }
      if (method == "fixture.vetoDone" && params.size() <= 4096) {
        f.veto_live = json::parse(params).value("ok", false) && root->get_window_state().status.ok;
        if (!f.veto_live) { fail("dom_veto_not_observed"); return "null"; }
        if (!root->dispatch([&] { spawn(0); }).ok) fail("spawn_dispatch");
        return "null";
      }
      fail("unexpected_root_method");
      return "null";
    };
    application app(settings, std::move(events));
    root = &app;
    const auto initialized = app.initialize();
    f.initialized = initialized.ok;
    if (!initialized.ok) f.failure = initialized.code;
    if (initialized.ok) {
      f.webview2 = app.get_capabilities().backend == "webview2";
      deadline budget(app, f);
      f.run = app.run().ok;
      f.quit = app.quit().ok;
    }
  } catch (const std::exception& e) { f.failure = e.what(); }
  catch (...) { f.failure = "unexpected_exception"; }
  bool ok = f.initialized && f.webview2 && f.run && f.quit && f.veto_live &&
    f.stale_counts_stable && !f.errors && f.failure.empty() && !f.timeout && !f.timeout_dispatch_failed;
  json windows = json::array();
  for (unsigned i = 0; i < f.windows.size(); ++i) {
    const auto& w = f.windows[i];
    ok = ok && w.ready == 1 && w.armed == 1 && w.closed == 1;
    if (i == 0) ok = ok && w.requests >= 1 && w.requests <= 3;
    else ok = ok && w.requests == 1 && w.reentrant_rejections == 2 &&
      w.retired_api_rejected && w.idempotent_close && w.callback_released;
    windows.push_back({{"kind", i == 0 ? "veto-root" : "approved-child"},
      {"ready", w.ready}, {"armed", w.armed}, {"requests", w.requests}, {"closed", w.closed},
      {"reentrantRejections", w.reentrant_rejections}, {"retiredApiRejected", w.retired_api_rejected},
      {"idempotentClose", w.idempotent_close}, {"callbackReleased", w.callback_released}});
  }
  std::cout << json{{"schema", "soui.windows-dom-close-regression/1"}, {"ok", ok},
    {"initialized", f.initialized}, {"backendWebView2", f.webview2}, {"run", f.run}, {"quit", f.quit},
    {"vetoLive", f.veto_live}, {"staleCountsStable", f.stale_counts_stable}, {"errors", f.errors},
    {"failure", f.failure}, {"timeout", f.timeout.load()},
    {"timeoutDispatchFailed", f.timeout_dispatch_failed.load()}, {"windows", windows}}.dump() << '\n';
  return ok ? 0 : 1;
}
