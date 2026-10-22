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

void evaluation_admission() {
  fixture test;
  auto& state = *test.state;
  state.renderer_reload_pending = false;
  state.committed = false;
  CHECK(check_evaluation(state).ok);  // Document-start bootstrap is still valid.
  state.renderer_reload_pending = true;
  CHECK(check_evaluation(state).code == "E_NAVIGATION");
  // A reload callback that did not supply a fresh load has no pending task,
  // but its cancelled evaluation channel must still reject new submissions.
  state.renderer_reload_pending = false;
  g_cancellable_cancel(state.evaluations);
  CHECK(check_evaluation(state).code == "E_NAVIGATION");
  fail_document(state);
  CHECK(check_evaluation(state).code == "E_LOAD");
  CHECK(check_ui(state).ok);  // Native/window operations remain available.

  // Model load() replacing the document channel, not actual engine recovery.
  ++state.generation;
  state.load_failed = false;
  g_clear_object(&state.evaluations);
  state.evaluations = g_cancellable_new();
  CHECK(check_evaluation(state).ok);
  result worker_result;
  std::thread worker([&] { worker_result = check_evaluation(state); });
  worker.join();
  CHECK(worker_result.code == "E_THREAD");
  shutdown(state, false);
  CHECK(check_evaluation(state).code == "E_CLOSED");
}
void evaluation_completion() {
  CHECK(!report_evaluation_error(nullptr));
  auto classification = [](GQuark domain, int code, bool expected) {
    // Identical text ensures classification cannot accidentally match a message.
    GError* error = g_error_new_literal(domain, code, "Unsupported result type");
    CHECK(report_evaluation_error(error) == expected);
    g_error_free(error);
  };
  classification(WEBKIT_JAVASCRIPT_ERROR, WEBKIT_JAVASCRIPT_ERROR_INVALID_RESULT, false);
  classification(WEBKIT_JAVASCRIPT_ERROR, WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED, true);
  classification(G_IO_ERROR, WEBKIT_JAVASCRIPT_ERROR_INVALID_RESULT, true);
  classification(G_IO_ERROR, G_IO_ERROR_CANCELLED, false);
  classification(WEBKIT_JAVASCRIPT_ERROR, G_IO_ERROR_CANCELLED, true);
  classification(G_IO_ERROR, G_IO_ERROR_FAILED, true);

  // Real JS engine, no GTK/display initialization. Submit unmodified source as
  // production does: even incomplete statements must remain syntax errors.
  JSCContext* context = jsc_context_new();
  auto run = [&](std::string script) {
    return jsc_context_evaluate(context, script.data(), static_cast<gssize>(script.size()));
  };
  JSCValue* value = jsc_context_evaluate(context, "Promise.resolve(1)", -1);
  CHECK(jsc_value_is_object(value));
  g_object_unref(value);
  for (const char* source : {"Promise.resolve(1)", "(function () {})",
                             "({ self: globalThis })"}) {
    value = run(source);
    CHECK(!jsc_context_get_exception(context));
    CHECK(jsc_value_is_object(value));
    g_object_unref(value);
  }
  value = run("42 // trailing comment");
  CHECK(!jsc_context_get_exception(context));
  CHECK(jsc_value_to_int32(value) == 42);
  g_object_unref(value);
  value = run("'use strict'; var globalVar = 7; let globalLexical = 11;");
  CHECK(!jsc_context_get_exception(context));
  g_object_unref(value);
  value = jsc_context_evaluate(context, "globalVar + globalLexical", -1);
  CHECK(!jsc_context_get_exception(context));
  CHECK(jsc_value_to_int32(value) == 18);
  g_object_unref(value);
  for (const char* source : {"'use strict'; undeclaredAssignment = 1;",
                             "throw new Error('real failure');",
                             "throw Promise.resolve(1);",
                             "const invalid = ;", "return 1;",
                             "if (true)", "while (false)"}) {
    value = run(source);
    CHECK(jsc_context_get_exception(context));
    if (value) g_object_unref(value);
    jsc_context_clear_exception(context);
  }
  g_object_unref(context);
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
  evaluation_admission();
  evaluation_completion();
  std::cout << "Linux document lifecycle: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
