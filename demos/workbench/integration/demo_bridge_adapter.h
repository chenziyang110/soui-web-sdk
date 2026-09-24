#pragma once
#include <soui/bridge_adapter.h>

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>
#include <soui/runtime/services/task/task_service.h>
#include "native_media_preview.h"

namespace soui::demo {

class demo_bridge_adapter : public soui::sdk::bridge_adapter {
 public:
  using event_sink = std::function<void(std::string_view)>;

  demo_bridge_adapter();
  ~demo_bridge_adapter();
  demo_bridge_adapter(const demo_bridge_adapter&) = delete;
  demo_bridge_adapter& operator=(const demo_bridge_adapter&) = delete;

  void set_owner_window(void* owner_window) noexcept;
  void record_paint() noexcept { paint_count_.fetch_add(1, std::memory_order_relaxed); }
  void set_present_count(std::uint64_t count) noexcept { present_count_.store(static_cast<unsigned>(count), std::memory_order_relaxed); }
  void set_display_refresh(unsigned rate) noexcept { display_refresh_.store(rate, std::memory_order_relaxed); }
  [[nodiscard]] unsigned requested_frame_rate() const noexcept { return requested_frame_rate_.load(); }
  [[nodiscard]] unsigned active_frame_rate() const noexcept { return active_frame_rate_.load(); }
  void set_active_frame_rate(unsigned rate) noexcept { active_frame_rate_.store(rate); }
  void set_draw_interval(unsigned interval_ms) noexcept { draw_interval_ms_.store(interval_ms, std::memory_order_relaxed); }
  void set_gpu_active(bool active) noexcept { gpu_active_.store(active, std::memory_order_relaxed); }
  void set_event_sink(event_sink sink);
  void shutdown_session(const std::string& session_id);
  [[nodiscard]] std::string handle_json(std::string_view json_text, bool native_user_gesture = false);
  [[nodiscard]] std::string issue_session(const std::string& session_id, const std::string& origin,
                                          const std::vector<std::string>& capabilities);
  [[nodiscard]] std::string register_root(const soui::runtime::scan_request_context& context,
                                          const std::filesystem::path& root);
  [[nodiscard]] soui::runtime::bridge_response handle(const soui::runtime::bridge_request& request) const;
  [[nodiscard]] soui::runtime::scan_response read_scan_page(const soui::runtime::scan_request_context& context,
                                                             const std::string& task_id,
                                                             std::string cursor,
                                                             std::size_t page_size = 100);

 private:
  struct subscription_binding {
    soui::runtime::scan_request_context context;
    std::string task_id;
    std::string event_name;
  };
  struct request_rate_window {
    std::chrono::steady_clock::time_point started;
    std::size_t count{0};
  };
  struct media_job {
    soui::runtime::scan_request_context context;
    std::string task_id;
    std::filesystem::path source;
    std::filesystem::path output;
    std::filesystem::path encoder;
    std::filesystem::path save_destination;
    std::string format;
    std::string codec;
    std::string operation;
    unsigned quality{82};
    std::atomic<bool> cancel_requested{false};
    std::thread worker;
  };
  struct media_file_binding {
    soui::runtime::scan_request_context context;
    std::filesystem::path path;
    std::string name;
    std::uintmax_t size{0};
  };
  struct workload_job {
    soui::runtime::scan_request_context context;
    std::string task_id;
    std::string kind;
    std::size_t complexity{0};
    std::atomic<bool> cancel_requested{false};
    std::string metrics_json;
    std::thread worker;
  };
  struct media_preview_job {
    soui::runtime::scan_request_context context;
    std::string task_id;
    std::filesystem::path source;
    std::atomic<bool> cancel_requested{false};
    media_preview_result result;
    std::thread worker;
  };

  void register_methods();
  [[nodiscard]] static std::string string_param(const std::string& params, const char* name);
  [[nodiscard]] static bool bool_param(const std::string& params, const char* name);
  [[nodiscard]] static std::size_t size_param(const std::string& params, const char* name,
                                              std::size_t fallback);
  [[nodiscard]] bool allow_request(const std::string& session_id);
  [[nodiscard]] static std::string augment_context(const soui::runtime::bridge_request& request);
  [[nodiscard]] bool owns_media_task(const soui::runtime::scan_request_context& context,
                                     const std::string& task_id) const;
  [[nodiscard]] std::string media_task_json(const soui::runtime::scan_request_context& context,
                                            const std::string& task_id) const;
  [[nodiscard]] bool owns_workload_task(const soui::runtime::scan_request_context& context,
                                         const std::string& task_id) const;
  [[nodiscard]] std::string workload_task_json(const soui::runtime::scan_request_context& context,
                                                const std::string& task_id) const;
  [[nodiscard]] bool owns_media_preview_task(const soui::runtime::scan_request_context& context,
                                              const std::string& task_id) const;
  [[nodiscard]] std::string media_preview_task_json(const soui::runtime::scan_request_context& context,
                                                    const std::string& task_id) const;
  [[nodiscard]] static std::filesystem::path bundled_sample_mp4();
  [[nodiscard]] static std::filesystem::path bundled_4k_sample_mp4();
  void run_media_job(media_job* job);
  void emit_media_event(const media_job& job, const std::string& state, unsigned progress,
                        const std::string& message) const;
  void join_finished_media_jobs();
  void shutdown_media_jobs(const std::string& session_id);
  void shutdown_media_files(const std::string& session_id);
  [[nodiscard]] std::string start_media_job(const soui::runtime::scan_request_context& context,
                                            const std::string& source, const std::string& format,
                                            const std::string& codec, unsigned quality,
                                            const std::string& operation,
                                            const std::string& operation_id);
  void run_workload_job(workload_job* job);
  void emit_workload_event(const workload_job& job, const std::string& state, unsigned progress,
                           const std::string& message) const;
  void join_finished_workload_jobs();
  void shutdown_workload_jobs(const std::string& session_id);
  void run_media_preview_job(media_preview_job* job);
  void emit_media_preview_event(const media_preview_job& job, const std::string& state,
                                unsigned progress, const std::string& message) const;
  void join_finished_media_preview_jobs();
  void shutdown_media_preview_jobs(const std::string& session_id);

  void* owner_window_{nullptr};
  std::atomic<std::uint64_t> paint_count_{0};
  std::atomic<unsigned> present_count_{0};
  std::atomic<unsigned> display_refresh_{0};
  std::atomic<unsigned> requested_frame_rate_{120}, active_frame_rate_{60};
  std::atomic<unsigned> draw_interval_ms_{17};
  std::atomic<bool> gpu_active_{false};
  std::atomic<unsigned> preferred_frame_rate_{120};
  mutable soui::runtime::permission_store permissions_;
  mutable soui::runtime::task_store tasks_;
  mutable soui::runtime::directory_scanner scanner_;
  mutable soui::runtime::directory_picker picker_;
  mutable soui::runtime::window_service windows_;
  mutable soui::runtime::clipboard_service clipboard_;
  mutable soui::runtime::capability_registry registry_;
  mutable soui::runtime::bridge_transport transport_;
  mutable std::mutex subscription_mutex_;
  std::unordered_map<std::string, subscription_binding> subscriptions_;
#ifdef SOUI_PROGRESS_DEDUP
  std::unordered_map<std::string, std::string> running_payloads_;
#endif
  std::uint64_t next_subscription_{1};
  event_sink event_sink_;
  std::string active_bootstrap_session_;
  std::unordered_map<std::string, request_rate_window> request_rates_;
  mutable std::mutex media_mutex_;
  std::unordered_map<std::string, std::unique_ptr<media_job>> media_jobs_;
  std::unordered_map<std::string, media_file_binding> media_files_;
  std::uint64_t next_media_file_{1};
  mutable std::uint64_t next_media_event_sequence_{1};
  mutable std::mutex workload_mutex_;
  std::unordered_map<std::string, std::unique_ptr<workload_job>> workload_jobs_;
  mutable std::mutex media_preview_mutex_;
  std::unordered_map<std::string, std::unique_ptr<media_preview_job>> media_preview_jobs_;
  soui::runtime::task_service public_tasks_;
};

}  // namespace soui::demo
