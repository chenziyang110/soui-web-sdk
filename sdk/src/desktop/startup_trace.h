#pragma once

// Private, opt-in diagnostic policy. Keep the SDK and template copies identical.
// Distinct namespaces give independently configured components separate state.
#ifndef SOUI_STARTUP_TRACE_NAMESPACE
#define SOUI_STARTUP_TRACE_NAMESPACE soui_sdk_startup_trace
#endif

#if defined(SOUI_STARTUP_TRACE_ENABLED) && SOUI_STARTUP_TRACE_ENABLED && \
    (defined(__APPLE__) || defined(SOUI_STARTUP_TRACE_TEST))
#include "startup_trace_config.h"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace SOUI_STARTUP_TRACE_NAMESPACE {

enum class stage : unsigned {
  app_main_enter,
  app_initialize_enter,
  app_initialize_return,
  app_run_enter,
  app_run_return,
  app_quit_enter,
  app_quit_return,
  app_destroy_enter,
  app_destroy_return,
  app_error_ui_enter,
  app_error_ui_return,
  app_exit,
  dialog_appkit_enter,
  dialog_appkit_return,
  dialog_screens_enter,
  dialog_screens_return,
  dialog_modal_enter,
  dialog_modal_return,
  sdk_listener_enter,
  sdk_listener_return,
  mac_shared_application_enter,
  mac_shared_application_return,
  mac_activation_enter,
  mac_activation_return,
  mac_webview_enter,
  mac_webview_return,
  mac_window_enter,
  mac_window_return,
  mac_show_enter,
  mac_show_return,
  mac_activate_enter,
  mac_activate_return,
  mac_load_enter,
  mac_load_return,
  mac_run_enter,
  mac_run_return
};

#if defined(SOUI_STARTUP_TRACE_ENABLED) && SOUI_STARTUP_TRACE_ENABLED && \
    (defined(__APPLE__) || defined(SOUI_STARTUP_TRACE_TEST))
namespace detail {

struct record {
  const char* text;
  std::size_t size;
};

template <std::size_t N>
constexpr record literal(const char (&text)[N]) noexcept {
  return {text, N - 1};
}

// No arbitrary text, paths, error details, or runtime values enter a record.
inline constexpr record records[] = {
  literal("SOUI_STARTUP_TRACE/1 app_main_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_initialize_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_initialize_return\n"),
  literal("SOUI_STARTUP_TRACE/1 app_run_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_run_return\n"),
  literal("SOUI_STARTUP_TRACE/1 app_quit_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_quit_return\n"),
  literal("SOUI_STARTUP_TRACE/1 app_destroy_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_destroy_return\n"),
  literal("SOUI_STARTUP_TRACE/1 app_error_ui_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 app_error_ui_return\n"),
  literal("SOUI_STARTUP_TRACE/1 app_exit\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_appkit_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_appkit_return\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_screens_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_screens_return\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_modal_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 dialog_modal_return\n"),
  literal("SOUI_STARTUP_TRACE/1 sdk_listener_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 sdk_listener_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_shared_application_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_shared_application_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_activation_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_activation_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_webview_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_webview_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_window_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_window_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_show_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_show_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_activate_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_activate_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_load_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_load_return\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_run_enter\n"),
  literal("SOUI_STARTUP_TRACE/1 mac_run_return\n")
};
inline constexpr std::size_t stage_count = sizeof(records) / sizeof(records[0]);
inline constexpr std::size_t maximum_bytes = 16 * 1024;
static_assert(stage_count == static_cast<unsigned>(stage::mac_run_return) + 1);
constexpr std::size_t whitelist_bytes() noexcept {
  std::size_t bytes = 0;
  for (const auto& entry : records) bytes += entry.size;
  return bytes;
}
static_assert(whitelist_bytes() <= maximum_bytes);

struct sink_state {
  std::atomic_flag busy = ATOMIC_FLAG_INIT;
  bool attempted = false;
  int descriptor = -1;
  dev_t device = 0;
  ino_t inode = 0;
  off_t bytes = 0;
  bool seen[stage_count]{};
};

// No destructor: late application/static teardown may still mark a stage.
// The OS closes the owned descriptor and releases its lock at process exit.
inline sink_state process_sink{};

inline void disable(sink_state& sink) noexcept {
  const int descriptor = sink.descriptor;
  sink.descriptor = -1;
  if (descriptor >= 0) ::close(descriptor);  // Never retry close after EINTR.
}

inline bool acceptable(const struct stat& info) noexcept {
  return S_ISREG(info.st_mode) && info.st_uid == ::getuid() &&
    info.st_nlink == 1 && (info.st_mode & 07777) == 0600;
}

inline bool unchanged(const sink_state& sink, const struct stat& info) noexcept {
  return acceptable(info) && info.st_dev == sink.device &&
    info.st_ino == sink.inode && info.st_size == sink.bytes;
}

inline void initialize(sink_state& sink) noexcept {
  sink.attempted = true;
  // O_NONBLOCK also prevents a mistaken FIFO from blocking before fstat.
  sink.descriptor = ::open(SOUI_STARTUP_TRACE_FILE,
    O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (sink.descriptor < 0) return;
  struct stat info{};
  if (::flock(sink.descriptor, LOCK_EX | LOCK_NB) != 0 ||
      ::fstat(sink.descriptor, &info) != 0 || !acceptable(info) || info.st_size != 0) {
    disable(sink);
    return;
  }
  sink.device = info.st_dev;
  sink.inode = info.st_ino;
}

}  // namespace detail

inline void mark(stage value) noexcept {
  const auto index = static_cast<unsigned>(value);
  if (index >= detail::stage_count) return;
  auto& sink = detail::process_sink;
  // Contending marks are dropped, not queued or retried on a UI thread.
  if (sink.busy.test_and_set(std::memory_order_acquire)) return;
  struct release final {
    detail::sink_state& sink;
    int saved_errno;
    ~release() noexcept {
      errno = saved_errno;
      sink.busy.clear(std::memory_order_release);
    }
  } unlock{sink, errno};
  if (sink.seen[index]) return;
  sink.seen[index] = true;
  if (!sink.attempted) detail::initialize(sink);
  if (sink.descriptor < 0) return;

  const auto& entry = detail::records[index];
  struct stat descriptor_info{}, path_info{};
  // The configured file and its ancestors must remain trusted and immutable
  // externally. These checks and flock are not a filesystem security sandbox.
  if (::fstat(sink.descriptor, &descriptor_info) != 0 ||
      !detail::unchanged(sink, descriptor_info) ||
      ::lstat(SOUI_STARTUP_TRACE_FILE, &path_info) != 0 ||
      !detail::unchanged(sink, path_info) || sink.bytes < 0 ||
      sink.bytes > static_cast<off_t>(detail::maximum_bytes - entry.size)) {
    detail::disable(sink);
    return;
  }
  if (::write(sink.descriptor, entry.text, entry.size) != static_cast<ssize_t>(entry.size)) {
    detail::disable(sink);  // No retry, alternate destination, or partial-line repair.
    return;
  }
  sink.bytes += static_cast<off_t>(entry.size);
}
#else
inline void mark(stage) noexcept {}
#endif

}  // namespace SOUI_STARTUP_TRACE_NAMESPACE
