#pragma once

#include <soui/runtime/bridge/action_contract.h>

#include <Windows.h>
#include <UIAutomationCore.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace soui::runtime {

enum class uia_role { button, input, selection_list, selection_item, task_status };
enum class uia_operation { invoke, set_value, select, focus };
enum class uia_capability { available, system_api_unavailable, com_unavailable, closed };

// Native registration of an existing soui.action/1 business method. This is
// neither a wire schema nor an authorization. The application owns dispatch.
struct uia_action_binding {
  std::string method;
  std::shared_ptr<const action_contract> contract;
  std::string parameters_json{"{}"};  // Invoke, Select or Focus input.
  std::string value_parameter;         // Existing SetValue input property name.
};

struct uia_node {
  std::string id;
  std::string parent_id;  // Only selection_item has a selection_list parent.
  uia_role role{uia_role::task_status};
  std::wstring name;
  std::wstring value;
  std::wstring status;
  RECT screen_bounds{};  // Actual owner-supplied physical screen coordinates.
  bool enabled{true};
  bool busy{false};
  bool offscreen{false};
  bool secret{false};
  bool read_only{false};
  bool selected{false};
  uia_action_binding action;
  uia_action_binding focus;
};

// Trusted native projection of the existing soui.gui/1 meanings. No DOM or
// page-supplied generation, state, callback or credential enters this API.
struct uia_projection {
  std::uint64_t document_generation{};
  std::wstring name;
  RECT screen_bounds{};
  std::string focused_id;
  std::vector<uia_node> nodes;
};

struct uia_action_request {
  std::uint64_t document_generation{};
  std::uint64_t incarnation{};
  std::string node_id;
  std::string method;
  std::string parameters_json;
  uia_operation operation{uia_operation::invoke};
  [[nodiscard]] bool native_user_gesture() const noexcept { return false; }
};

struct uia_action_result {
  HRESULT result{E_ACCESSDENIED};
  std::string output_json{"{}"};
};

// Opt-in Soui::Uia module. The owner must already have initialized COM and must
// own HWND on its actual current thread. Callbacks only run in pump() on that
// owner, outside shared-state locks. Default/empty policy denies every command.
// They must enter registered typed native business methods, with application
// authorization; UIA supplies no Agent scope, bearer or native user gesture.
class windows_uia_provider final {
 public:
  static constexpr std::size_t max_nodes{256};
  static constexpr std::size_t max_public_bytes{64U * 1024U};
  static constexpr std::size_t max_value_units{4096};
  static constexpr std::size_t max_pending{64};
  static constexpr DWORD synchronous_wait_ms{250};
  using policy = std::function<bool(const uia_action_request&)>;
  using dispatcher = std::function<uia_action_result(const uia_action_request&)>;

  [[nodiscard]] static HRESULT create(HWND window, UINT wake_message,
                                      std::uint64_t document_generation,
                                      policy allow, dispatcher dispatch,
                                      std::unique_ptr<windows_uia_provider>& result) noexcept;
  ~windows_uia_provider();
  windows_uia_provider(const windows_uia_provider&) = delete;
  windows_uia_provider& operator=(const windows_uia_provider&) = delete;
  windows_uia_provider(windows_uia_provider&&) = delete;
  windows_uia_provider& operator=(windows_uia_provider&&) = delete;

  // All controller methods require the actual HWND owner thread. Publish is
  // atomic and bounded. A new document requires close then a new controller.
  [[nodiscard]] HRESULT publish(const uia_projection& projection) noexcept;
  // At most eight commands per call. WndProc must forward the wake message;
  // an ordinary owner pump may also call this. Never call while holding a UI lock.
  [[nodiscard]] HRESULT pump() noexcept;
  // Handles only the configured wake message and UIA WM_GETOBJECT. Preserves
  // original wParam/lParam. Unavailable/closed UIA WM_GETOBJECT returns 0.
  [[nodiscard]] bool handle_message(UINT message, WPARAM wparam, LPARAM lparam,
                                    LRESULT& result) noexcept;
  // The native test/application may query a provider; returned COM ref is owned.
  [[nodiscard]] HRESULT root(IRawElementProviderSimple** result) noexcept;
  [[nodiscard]] uia_capability capability() const noexcept;
  // Revoke immediately; S_FALSE means callback/API borrows still hold HWND and
  // this controller. Retry on owner until S_OK before destroying either object.
  // Close inside its own callback is pending. Revoke never rolls back a commit.
  [[nodiscard]] HRESULT close() noexcept;

 private:
  struct impl;
  explicit windows_uia_provider(std::unique_ptr<impl> state) noexcept;
  std::unique_ptr<impl> state_;
};

}  // namespace soui::runtime
