#pragma once

// Optional SOUI glue. Ordinary Win32 users include windows_owner_loop.h only;
// the Core library does not acquire a SOUI link or header dependency.
#include <souistd.h>
#include <SApp.h>
#include <core/SMsgLoop.h>
#include <soui/host/windows_owner_loop.h>
#include <climits>
#include <memory>
#include <stdexcept>

namespace soui::sdk {
namespace detail {

class soui_owner_message_loop final : public SOUI::SMessageLoop {
 public:
  explicit soui_owner_message_loop(std::shared_ptr<windows_owner_loop> loop)
      : loop_(std::move(loop)) { m_msg = MSG{}; }

  int Run() override {
    if (m_bRunning) throw std::logic_error("The same SOUI loop cannot reenter Run");
    m_bRunning = TRUE;
    m_bQuit = FALSE;
    struct running_scope final {
      BOOL& running;
      ~running_scope() { running = FALSE; }
    } running{m_bRunning};
    // Retain the actual loop object across all application callbacks.
    const auto loop = loop_;
    windows_owner_loop_callbacks callbacks;
    callbacks.message_retrieved = [this](const MSG& message) { m_msg = message; };
    callbacks.dispatch = [this](MSG& message) {
      m_msg = message;
      OnMsg(&m_msg); // Preserve filters, pretranslation and application dispatch.
      message = m_msg;
    };
    callbacks.idle = [this](std::uint32_t count) {
      const auto bounded = count > static_cast<std::uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(count);
      return OnIdle(bounded) != FALSE;
    };
    callbacks.is_idle_message = [](const MSG& message) {
      auto copy = message;
      return SOUI::SMessageLoop::IsIdleMessage(&copy) != FALSE;
    };
    callbacks.quit_requested = [this] { return m_bQuit != FALSE; };
    callbacks.get_message_error = [](std::uint32_t) {
      return true; // The vendor loop's explicit GetMessage(-1) retry policy.
    };
    const auto result = loop->run(callbacks);
    return m_bQuit ? static_cast<int>(m_msg.wParam) : result;
  }

 private:
  std::shared_ptr<windows_owner_loop> loop_;
};

class soui_owner_loop_factory final : public SOUI::TObjRefImpl<SOUI::IMsgLoopFactory> {
 public:
  explicit soui_owner_loop_factory(const std::shared_ptr<windows_owner_loop>& main_loop)
      : prepared_main_(std::make_unique<soui_owner_message_loop>(main_loop)) {}

  SOUI::SMessageLoop* CreateMsgLoop() override {
    if (prepared_main_) return prepared_main_.release();
    return new soui_owner_message_loop(std::make_shared<windows_owner_loop>());
  }
  void DestoryMsgLoop(SOUI::SMessageLoop* loop) override { delete loop; }

 private:
  // SApplication destroys its old loop before CreateMsgLoop. Allocate the
  // replacement first so that the initial Create cannot fail after destruction.
  std::unique_ptr<soui_owner_message_loop> prepared_main_;
};

}  // namespace detail

// A fresh application with SDK-owned main and dialog loops. It deliberately
// accepts the default system registrar only: it never discovers/replaces an
// existing application's custom factory, filters or previously saved loop.
// Custom applications keep SApplication and integrate the Win32 loop explicitly.
class soui_owner_application final : public SOUI::SApplication {
 public:
  soui_owner_application(SOUI::IRenderFactory* renderer, HINSTANCE instance,
                         LPCTSTR host_class = _T("SOUIHOST"), BOOL ime_application = FALSE)
      : SOUI::SApplication(renderer, instance, host_class, SOUI::SObjectDefaultRegister(), ime_application) {
    const auto original = GetMsgLoop();
    if (!original || original->IsRunning() || original->m_aMsgFilter.GetCount() || original->m_aIdleHandler.GetCount())
      throw std::logic_error("SDK SOUI loops require a fresh application before loop registration");
    auto main_loop = std::make_shared<windows_owner_loop>();
    SOUI::CAutoRefPtr<SOUI::IMsgLoopFactory> factory;
    factory.Attach(new detail::soui_owner_loop_factory(main_loop));
    if (!SetMsgLoopFactory(factory))
      throw std::runtime_error("Cannot install the SDK loop on the newly constructed SOUI application");
    main_loop_ = main_loop;
  }

  [[nodiscard]] host_result owner_loop_status() const {
    const auto loop = main_loop_.lock();
    return loop ? loop->status()
                : host_result{false, "E_OWNER_LOOP_REPLACED", "The SDK main loop no longer exists."};
  }

 private:
  std::weak_ptr<windows_owner_loop> main_loop_;
};

}  // namespace soui::sdk
