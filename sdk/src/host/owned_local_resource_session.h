#pragma once

#include <soui/host/webview_host.h>
#include "local_resource_snapshot.h"
#include "miniblink_local_resource_abi.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace soui::sdk::detail {

// Private Host integration. The publication has one real thread handle and is
// retained through timeout/cancellation until that thread has actually exited.
class owned_local_resource_session final {
 public:
  using validator = std::function<host_result(const local_resource_snapshot&)>;
  owned_local_resource_session();
  ~owned_local_resource_session();
  owned_local_resource_session(const owned_local_resource_session&) = delete;
  owned_local_resource_session& operator=(const owned_local_resource_session&) = delete;

  [[nodiscard]] host_result prepare(const std::filesystem::path&, std::string entry, validator,
      std::uint64_t startup_deadline_ms);
  [[nodiscard]] bool publication_retired() const noexcept;
  [[nodiscard]] bool release_unbound_publication();
  // UI revocation is atomic and never invokes WKE or the Engine from that owner.
  void revoke_admission() noexcept;
  [[nodiscard]] bool begin_reload() noexcept;
  [[nodiscard]] bool preparation_expired() const noexcept;
  [[nodiscard]] std::uint64_t startup_deadline_ms() const noexcept { return startup_deadline_ms_; }

  // Native methods run only on the existing Engine owner. Each retirement call
  // makes at most one beginClose or queryDrain call; it never waits or reposts.
  [[nodiscard]] host_result bind_on_engine(void* module, void* view, SouiLocalResourceEndpoint,
      std::uint64_t view_generation, std::uint64_t broker_document_generation);
  [[nodiscard]] host_result retire_on_engine(bool final_close);
  void observe_on_engine();
  [[nodiscard]] bool payload_retired() const noexcept;
  [[nodiscard]] bool physically_retired() const noexcept;
  [[nodiscard]] host_result terminal_result() const;
  [[nodiscard]] bool failed() const noexcept { return failure_code_.load(std::memory_order_acquire) != nullptr; }
  [[nodiscard]] std::int32_t native_error() const noexcept { return native_error_.load(std::memory_order_acquire); }

  [[nodiscard]] std::string entry_url() const;
  [[nodiscard]] bool allow_navigation(void* view, std::string_view url) noexcept;
  [[nodiscard]] bool trusted_entry(void* view, std::string_view url) const noexcept;
  [[nodiscard]] std::uint64_t native_document_receipt() const noexcept;
  [[nodiscard]] bool acknowledge_context(void* view, std::string_view url,
      std::uint64_t native_receipt, std::uint64_t context_receipt) noexcept;
  [[nodiscard]] bool accepts_context(void* view, std::uint64_t context_receipt) const noexcept;
  [[nodiscard]] bool accepts_context(std::uint64_t context_receipt) const noexcept;
  [[nodiscard]] bool admission_open() const noexcept { return admission_.load(std::memory_order_acquire); }
#ifdef SOUI_LOCAL_RESOURCE_HOST_TEST_ONLY
  // The standalone source fixture replaces only discovery, then exercises the
  // same production bind/receipt/retirement code and actual publication thread.
  void test_api(SouiLocalResourceApi value) noexcept { test_api_ = value; }
  [[nodiscard]] void* test_publication_handle() const noexcept;
#endif

 private:
  struct publication;
  std::unique_ptr<publication> publication_;
  mutable std::mutex publication_mutex_;
  std::shared_ptr<const local_resource_snapshot> snapshot_;
  SouiLocalResourceApi api_{};
  SouiLocalResourceEndpoint endpoint_{};
  SouiLocalResourceReceipt receipt_{};
  std::string host_nonce_;
  std::uint64_t close_sequence_{};
  bool close_requested_{false};
  std::atomic<bool> native_call_active_{false}; // Also visible to the UI's physical-owner observation.
  bool final_close_{false};
  std::uint64_t startup_deadline_ms_{};
  mutable std::mutex identity_mutex_;
  void* view_{};
  std::string entry_;
  std::uint64_t admission_epoch_{1}, pending_bind_epoch_{1}; // Protected by identity_mutex_.
  std::atomic<std::uint64_t> native_receipt_{0}, context_receipt_{0};
  std::atomic<bool> admission_{false}, retired_{true}, load_armed_{false};
  std::atomic<const char*> failure_code_{nullptr};
  std::atomic<std::int32_t> native_error_{0};
#ifdef SOUI_LOCAL_RESOURCE_HOST_TEST_ONLY
  SouiLocalResourceApi test_api_{};
#endif

  void fail(const char*) noexcept;
  void close_admission(bool invalidate_transition) noexcept;
  [[nodiscard]] bool accept_drain(std::uint32_t, const SouiLocalResourceDrain&, bool closing);
};

} // namespace soui::sdk::detail
