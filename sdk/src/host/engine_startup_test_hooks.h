#pragma once

#ifndef SOUI_ENGINE_STARTUP_TEST_ONLY
#error "This private seam is only available to the engine startup regression target"
#endif
#ifndef _WIN32
#error "The engine startup event regression requires Windows"
#endif

#include <windows.h>

namespace soui::sdk::detail::engine_startup_test {

// Supplied by the independently compiled regression fixture, never the SDK.
HANDLE create_wake_event() noexcept;
void close_wake_event(HANDLE event) noexcept;
void before_worker_launch();

}  // namespace soui::sdk::detail::engine_startup_test
