// Exercise production callback/queue code without starting GTK or a renderer.
// This target includes the backend directly; do not also link Soui::Desktop.
#include "../../sdk/src/desktop/backends/linux_webkitgtk.cpp"

#include <iostream>

namespace {
using namespace soui::desktop;
using namespace soui::desktop::detail;

int failures = 0;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
  } } while (false)

constexpr const char* selected_uri = "file:///selected/index.html";

struct fixture {
  std::shared_ptr<linux_state> state{std::make_shared<linux_state>()};
  int document_calls{0};
  int native_calls{0};
  int errors{0};
  std::string code;

  fixture() {
    state->active.store(true);
    state->generation = 12;
    state->committed = true;
    state->navigation_ticket = true;
    state->renderer_reload_pending = true;
    state->evaluations = g_cancellable_new();
    state->events.allow_navigation = [](std::string_view uri) { return uri == selected_uri; };
    state->events.error = [this](result failure) { ++errors; code = failure.code; };
  }
  ~fixture() { shutdown(*state, false); }

  void queue() {
    CHECK(enqueue_document(*state, [this](linux_state&) { ++document_calls; }).ok);
    CHECK(enqueue(*state, [this] { ++native_calls; }).ok);
  }
  void pump() {
    for (int i = 0; i < 16 && g_main_context_pending(state->context); ++i)
      g_main_context_iteration(state->context, FALSE);
    CHECK(state->dispatch_sources.empty());
  }
  void failed() {
    CHECK(state->load_failed);
    CHECK(!state->committed);
    CHECK(!state->navigation_ticket);
    CHECK(!state->renderer_reload_pending);
    CHECK(state->generation == 13);
    CHECK(g_cancellable_is_cancelled(state->evaluations));
  }
};

void successful_dispatch() {
  fixture test;
  test.queue();
  test.pump();
  CHECK(test.document_calls == 1);
  CHECK(test.native_calls == 1);
  CHECK(test.errors == 0);
}

void terminated_document() {
  fixture test;
  test.queue();
  // Application error handling can enter a nested loop. Invalidation must
  // already be complete before any queued callback gets that opportunity.
  test.state->events.error = [&](result failure) {
    ++test.errors;
    test.code = failure.code;
    test.failed();
    test.pump();
  };
  process_terminated(nullptr, WEBKIT_WEB_PROCESS_CRASHED, test.state.get());
  CHECK(test.errors == 1 && test.code == "E_WEB_PROCESS");
  CHECK(test.document_calls == 0);
  CHECK(test.native_calls == 1);
  // A late load event cannot resurrect the crashed document. Null view is
  // intentional: no WebView may be consulted after the terminal guard.
  load_changed(nullptr, WEBKIT_LOAD_STARTED, test.state.get());
  load_changed(nullptr, WEBKIT_LOAD_COMMITTED, test.state.get());
  load_changed(nullptr, WEBKIT_LOAD_FINISHED, test.state.get());
  test.failed();
  test.queue();
  test.pump();
  CHECK(test.document_calls == 0);
  CHECK(test.native_calls == 2);
}

void failed_document() {
  fixture test;
  test.queue();
  GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "fixture load failure");
  CHECK(load_failed(nullptr, WEBKIT_LOAD_COMMITTED, selected_uri, error, test.state.get()));
  g_error_free(error);
  test.failed();
  CHECK(test.errors == 1 && test.code == "E_LOAD");
  test.pump();
  CHECK(test.document_calls == 0);
  CHECK(test.native_calls == 1);
}

void expected_cancellation(GQuark domain, int code) {
  fixture test;
  test.queue();
  GError* error = g_error_new_literal(domain, code, "fixture cancellation");
  CHECK(load_failed(nullptr, WEBKIT_LOAD_STARTED, selected_uri, error, test.state.get()));
  g_error_free(error);
  CHECK(!test.state->load_failed && test.state->committed);
  CHECK(test.state->generation == 12);
  CHECK(!g_cancellable_is_cancelled(test.state->evaluations));
  test.pump();
  CHECK(test.document_calls == 1 && test.native_calls == 1 && test.errors == 0);
}

void unrelated_failure() {
  fixture test;
  test.queue();
  GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "old URI failed");
  CHECK(load_failed(nullptr, WEBKIT_LOAD_STARTED, "file:///old/index.html", error, test.state.get()));
  g_error_free(error);
  CHECK(!test.state->load_failed && test.state->generation == 12);
  test.pump();
  CHECK(test.document_calls == 1 && test.native_calls == 1 && test.errors == 0);
}

void fresh_generation() {
  fixture test;
  test.queue();
  process_terminated(nullptr, WEBKIT_WEB_PROCESS_CRASHED, test.state.get());
  // Model the native load boundary only. This is not a real WebKit recovery
  // test: a fresh generation must not revive already queued document work.
  ++test.state->generation;
  test.state->load_failed = false;
  CHECK(enqueue_document(*test.state, [&](linux_state&) { test.document_calls += 10; }).ok);
  test.pump();
  CHECK(test.document_calls == 10 && test.native_calls == 1);
  shutdown(*test.state, false);
  const auto generation = test.state->generation;
  process_terminated(nullptr, WEBKIT_WEB_PROCESS_CRASHED, test.state.get());
  CHECK(test.errors == 1 && test.state->generation == generation);
}
}  // namespace

int main() {
  successful_dispatch();
  terminated_document();
  failed_document();
  expected_cancellation(WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED);
  expected_cancellation(G_IO_ERROR, G_IO_ERROR_CANCELLED);
  expected_cancellation(WEBKIT_POLICY_ERROR,
                        WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE);
  unrelated_failure();
  fresh_generation();
  std::cout << "Linux document lifecycle: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
