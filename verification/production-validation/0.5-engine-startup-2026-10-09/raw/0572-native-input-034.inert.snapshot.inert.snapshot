#pragma once

#include <soui/host/engine_thread.h>
#include <soui/host/webview_host.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace soui::sdk::detail {

// invoke's timeout ends the wait, not the accepted task. Both its result and
// work must remain owned by the task even after this caller has returned.
inline host_result invoke_diagnostic_result(
    engine_thread& engine, std::function<host_result()> work,
    host_result failure_result,
    std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
  if (!work) return failure_result;
  auto result = std::make_shared<host_result>();
  const bool completed = engine.invoke([result, work = std::move(work)] {
    *result = work();
  }, timeout);
  // Only a confirmed completion synchronizes access to the result. A timed
  // out or throwing task must never turn the diagnostic into success.
  if (!completed) return failure_result;
  return std::move(*result);
}

}  // namespace soui::sdk::detail
