#pragma once

namespace soui::sdk::detail {

// Engine retains shared ownership until owner-thread unregister or shutdown.
// Implementations publish pending before notifying the existing Engine event.
class engine_input_service {
 public:
  virtual ~engine_input_service() = default;
  [[nodiscard]] virtual bool pending() const noexcept = 0;
  virtual void service() noexcept = 0;
  virtual void quiesce() noexcept = 0;
  virtual void close() noexcept = 0;
};

}  // namespace soui::sdk::detail
