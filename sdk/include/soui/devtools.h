#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace soui::sdk {
class soui_miniblink_host;
namespace detail { struct devtools_host_state; }

namespace devtools {

enum class policy { disabled, application_controlled };
enum class termination { none, detach, destroy_target };
enum class truth { unknown, no, yes };
enum class phase { idle, opening, attached, revoking, drained, failed };
enum class operation_status { denied, accepted, pending, completed, failed };
enum class revoke_reason { application, target_close, document_change, grant_expired, application_exit };
enum class callback_domain { unknown, miniblink_session };

struct configuration {
  policy access{policy::disabled};
  termination ending{termination::none};
  std::filesystem::path frontend_root; // MiniBlink's matching immutable resource package.
  std::string frontend_manifest_sha256; // Native package identity, never inferred from the same directory.
  std::string kernel_sha256; // Exact loaded MiniBlink image paired with that frontend.
  bool f12{false};
  bool ctrl_shift_i{false};
};

struct capabilities {
  bool native_open{false};
  bool detach{false};
  bool destroy_target{false};
  truth attachment_observation{truth::unknown};
  truth pause_observation{truth::unknown};
  truth visibility_observation{truth::unknown};
  truth source_maps{truth::unknown}; // Presence is not evidence of correct original-source stepping.
};

struct operation_result {
  std::uint64_t id{};
  operation_status status{operation_status::denied};
  std::string code;
  std::string message;
  std::int32_t native_error{};
  [[nodiscard]] bool accepted() const noexcept {
    return status == operation_status::accepted || status == operation_status::pending ||
        status == operation_status::completed;
  }
};

struct callback_observation {
  callback_domain domain{callback_domain::unknown};
  bool sampled{false};
  // Scoped session observations, never all Engine/process callback coverage.
  std::uint32_t interrupt_pending{}, protocol_queued{}, resource_queued{};
  std::uint32_t dispatch_active{}, resource_active{}, frontend_paint_active{};
};

struct observation {
  phase state{phase::idle};
  truth attached{truth::unknown}, paused{truth::unknown}, visible{truth::unknown};
  bool grant_valid{false};
  bool retirement_required{false};
  bool resources_retained{false};
  std::uint64_t operation_id{};
  std::uint32_t pending_callbacks{}, active_callbacks{};
  callback_observation callbacks;
  std::int32_t native_error{};
  std::string code;
};

// Internal renderer receipt, never a permission or a bearer. Optional adapter
// implementations return this only to the protected native Host call chain.
struct target_identity {
  std::uint64_t generation{};
  std::string document;
  bool ready{false};
};

// Native-only opaque authority. A default value is unissued. No JSON, string,
// raw pointer or constructor can manufacture a grant for another Host.
class grant final {
 public:
  grant() = default;
 private:
  std::weak_ptr<detail::devtools_host_state> owner_;
  std::uint64_t epoch_{}, generation_{}, process_birth_{};
  std::uint32_t process_{};
  std::string document_;
  std::chrono::steady_clock::time_point expires_{};
  friend class ::soui::sdk::soui_miniblink_host;
};

} // namespace devtools
} // namespace soui::sdk
