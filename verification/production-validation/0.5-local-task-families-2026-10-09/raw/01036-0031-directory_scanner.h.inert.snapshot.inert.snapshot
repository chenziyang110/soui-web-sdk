#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <soui/runtime/core/result/result.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/permissions/permission_store.h>

namespace soui::runtime {

struct scan_request_context {
  std::string session_id;
  std::string origin;
  std::string token;
  // Optional native identity for retained application resources. Scanner entry
  // points always resolve token themselves; a caller-supplied value is untrusted.
  std::shared_ptr<const authorization_scope> authorization;
};

struct scan_entry {
  std::string relative_path;
  bool directory{false};
  std::uintmax_t size{0};
};

// Bounded native summary for completed scans. The extension key is lower-case
// and uses "[none]" for files without an extension. Directory entries are
// intentionally excluded from these counters.
struct scan_group_summary {
  std::string extension;
  std::size_t file_count{0};
  std::uintmax_t total_bytes{0};
};

struct scan_response {
  result_kind kind{result_kind::failure};
  std::string code;
  std::string message;
  std::string task_id;
  std::vector<scan_entry> entries;
  std::string next_cursor;
  bool has_more{false};
  std::size_t skipped_count{0};
  std::size_t error_count{0};
  std::vector<std::string> issues;
  std::size_t file_count{0};
  std::uintmax_t total_bytes{0};
  std::vector<scan_group_summary> groups;

  [[nodiscard]] bool ok() const noexcept { return kind == result_kind::success; }
};

class directory_scanner {
 public:
  static constexpr std::size_t max_workers_per_session{2};
  static constexpr std::size_t max_cached_entries{20000};
  static constexpr std::size_t max_retained_tasks_per_session{32};
  using event_sink = std::function<void(std::string_view)>;

  directory_scanner(const permission_store& permissions, task_store& tasks);
  ~directory_scanner();
  directory_scanner(const directory_scanner&) = delete;
  directory_scanner& operator=(const directory_scanner&) = delete;

  void set_event_sink(event_sink sink);
  void shutdown_session(const std::string& session_id);
  // Binds an opaque root reference to a user-authorized directory. The absolute path never
  // crosses the bridge and is retained only in this native service.
  [[nodiscard]] std::string register_root(const scan_request_context& context,
                                          const std::filesystem::path& root);
  [[nodiscard]] scan_response start(const scan_request_context& context, std::string operation_id,
                                   const std::string& root_reference, const std::string& filter = {},
                                   std::string operation_scope = "demo.scan.start");
  [[nodiscard]] scan_response read_page(const scan_request_context& context,
                                        const std::string& task_id, std::string cursor,
                                        std::size_t page_size = 100);
  [[nodiscard]] bool cancel(const scan_request_context& context, const std::string& task_id);
  [[nodiscard]] bool owns_task(const scan_request_context& context, const std::string& task_id) const;

 private:
  struct root_binding {
    std::string session_id;
    std::string origin;
    std::shared_ptr<const authorization_scope> authorization;
    std::filesystem::path root;
  };
  struct scan_state {
    scan_request_context context;
    std::vector<scan_entry> entries;
    std::string filter;
    result_kind kind{result_kind::input_required};
    std::string code;
    std::string message;
    bool complete{false};
    bool duplicate{false};
    std::size_t skipped_count{0};
    std::size_t error_count{0};
    std::vector<std::string> issues;
    std::size_t file_count{0};
    std::uintmax_t total_bytes{0};
    std::vector<scan_group_summary> groups;
  };
  struct scan_worker {
    std::atomic<bool> cancel_requested{false};
    std::thread thread;
  };

  [[nodiscard]] std::shared_ptr<const authorization_scope> resolve_authorization(
      const scan_request_context& context, const std::string& permission) const;
  [[nodiscard]] std::shared_ptr<const authorization_scope> resolve_scan_authorization(
      const scan_request_context& context) const;
  [[nodiscard]] static bool contains(const std::filesystem::path& root,
                                     const std::filesystem::path& candidate);
  [[nodiscard]] static std::size_t cursor_offset(const std::string& cursor);
  [[nodiscard]] scan_response error(std::string code, std::string message,
                                    std::string task_id = {}) const;
  void run_scan(std::string task_id, root_binding binding, scan_request_context context,
                std::string filter, scan_worker* control);
  void emit_event(const std::string& session_id, const std::string& task_id,
                  const std::string& name, std::uint32_t progress, const std::string& message = {}) const;
  void join_finished_workers();

  const permission_store& permissions_;
  task_store& tasks_;
  mutable std::mutex mutex_;
  std::uint64_t next_root_{1};
  std::unordered_map<std::string, root_binding> roots_;
  std::unordered_map<std::string, scan_state> scans_;
  std::unordered_map<std::string, std::unique_ptr<scan_worker>> workers_;
  mutable std::uint64_t next_event_sequence_{1};
  event_sink sink_;
};

}  // namespace soui::runtime
