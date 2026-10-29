#pragma once

#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace input_deadline_cases {
using namespace soui::sdk;
using clock = std::chrono::steady_clock;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline input_packet character(std::uint64_t sequence) {
  input_packet value; value.kind = input_kind::character; value.sequence = sequence; return value;
}

inline void run_mailbox() {
  const auto expired = (clock::time_point::min)();
  {
    std::vector<std::uint64_t> seen;
    detail::input_mailbox mailbox([&](const input_packet* input) { seen.push_back(input ? input->sequence : 0); });
    for (unsigned sequence = 1; sequence <= 3; ++sequence)
      check(mailbox.post(character(sequence)) == input_admission_result::accepted, "deadline input admission");
    mailbox.service_before_until(0, expired);
    check(seen == std::vector<std::uint64_t>({1}) && mailbox.pending() && mailbox.oldest_order() == 2,
        "expired input budget did not yield after one eligible packet");
    mailbox.service_before_until(2, expired);
    check(seen.size() == 1 && mailbox.oldest_order() == 2, "expired budget bypassed exclusive FIFO cutoff");
    mailbox.service_before_until(3, expired);
    check(seen == std::vector<std::uint64_t>({1, 2}) && mailbox.pending() && mailbox.oldest_order() == 3,
        "deadline continuation lost pending input order");
    mailbox.service_before_until(0, expired);
    check(seen == std::vector<std::uint64_t>({1, 2, 3}) && !mailbox.pending() && mailbox.oldest_order() == 0,
        "expired input service failed to make eventual progress");
  }
  {
    std::vector<std::uint64_t> seen;
    detail::input_mailbox mailbox([&](const input_packet* input) { seen.push_back(input ? input->sequence : 0); });
    check(mailbox.post(character(1)) == input_admission_result::accepted
        && mailbox.post(character(2)) == input_admission_result::accepted, "deadline recovery inputs");
    auto cancel = character(3); cancel.kind = input_kind::cancel;
    check(mailbox.post(cancel) == input_admission_result::accepted, "deadline recovery marker");
    mailbox.service_before_until(0, expired);
    mailbox.service_before_until(0, expired);
    check(seen == std::vector<std::uint64_t>({1, 2}) && mailbox.pending() && mailbox.oldest_order() == 3,
        "deadline yielded without retaining ordered recovery");
    mailbox.service_before_until(3, expired);
    check(seen.size() == 2 && mailbox.pending(), "recovery crossed FIFO cutoff after budget yield");
    mailbox.service_before_until(0, expired);
    check(seen == std::vector<std::uint64_t>({1, 2, 0}) && !mailbox.pending(), "deadline recovery did not retire once");
  }
  {
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    std::vector<std::uint64_t> seen;
    detail::input_mailbox mailbox([&](const input_packet* input) { seen.push_back(input ? input->sequence : 0); },
        [maximum](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
          const auto order = recovery ? maximum : 1;
          if (first) head.store(order, std::memory_order_release);
          pending.store(true, std::memory_order_release);
          return order;
        });
    check(mailbox.post(character(1)) == input_admission_result::accepted, "MAX deadline input");
    auto cancel = character(2); cancel.kind = input_kind::cancel;
    check(mailbox.post(cancel) == input_admission_result::accepted, "MAX deadline recovery marker");
    mailbox.service_before_until(maximum, expired);
    check(seen == std::vector<std::uint64_t>({1}) && mailbox.pending() && mailbox.oldest_order() == maximum,
        "expired MAX service did not retain recovery after older input");
    mailbox.service_before_until(maximum, expired);
    mailbox.service_before_until(maximum, expired);
    check(seen == std::vector<std::uint64_t>({1, 0}) && !mailbox.pending(),
        "MAX cleanup cutoff excluded or duplicated recovery after deadline yield");
  }
  {
    unsigned calls = 0;
    detail::input_mailbox mailbox([&](const input_packet*) { ++calls; });
    check(mailbox.post(character(1)) == input_admission_result::accepted
        && mailbox.post(character(2)) == input_admission_result::accepted, "quiescing deadline inputs");
    mailbox.quiesce();
    check(mailbox.post(character(3)) == input_admission_result::stopped, "quiescing mailbox accepted new input");
    mailbox.service_before_until(0, expired);
    check(calls == 1 && mailbox.pending(), "quiescing deadline lost accepted pending input");
    mailbox.close();
    mailbox.service_before_until(0, expired);
    check(calls == 1 && !mailbox.pending(), "closed deadline service dispatched or reopened input");
  }
  {
    unsigned calls = 0;
    detail::input_mailbox* selected = nullptr;
    detail::input_mailbox mailbox([&](const input_packet*) { ++calls; selected->close(); });
    selected = &mailbox;
    check(mailbox.post(character(1)) == input_admission_result::accepted
        && mailbox.post(character(2)) == input_admission_result::accepted, "reentrant deadline inputs");
    mailbox.service_before_until(0, expired);
    check(calls == 1 && !mailbox.pending(), "deadline callback close resumed the mailbox");
  }
  {
    unsigned calls = 0;
    detail::input_mailbox mailbox([&](const input_packet*) { ++calls; });
    for (unsigned sequence = 1; sequence <= 9; ++sequence)
      check(mailbox.post(character(sequence)) == input_admission_result::accepted, "legacy batch admission");
    mailbox.service_before(0);
    check(calls >= 1 && calls <= 8 && mailbox.pending(), "legacy entry lost the existing eight-item cap");
    for (unsigned turn = 0; turn < 9 && mailbox.pending(); ++turn) mailbox.service_before(0);
    check(calls == 9 && !mailbox.pending(), "legacy entry did not drain all accepted input");
  }
  {
    struct legacy_service final : detail::engine_input_service {
      unsigned calls{0}; std::uint64_t cutoff{0};
      bool pending() const noexcept override { return false; }
      void service() noexcept override { calls += 100; }
      void service_before(std::uint64_t order) noexcept override { ++calls; cutoff = order; }
      void quiesce() noexcept override {}
      void close() noexcept override {}
    } legacy;
    detail::engine_input_service& base = legacy;
    base.service_before_until(7, expired);
    check(legacy.calls == 1 && legacy.cutoff == 7, "deadline fallback bypassed the legacy service override");
  }
}

#ifdef _WIN32
class scoped_budget final {
 public:
  explicit scoped_budget(const wchar_t* value) {
    constexpr auto name = L"SOUI_ENGINE_SHARED_TURN_BUDGET_US";
    SetLastError(ERROR_SUCCESS);
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    present_ = size != 0 || GetLastError() == ERROR_SUCCESS;
    if (size) {
      previous_.resize(size);
      const auto copied = GetEnvironmentVariableW(name, previous_.data(), size);
      check(copied < size, "input deadline fixture environment changed during snapshot");
      previous_.resize(copied);
    }
    check(SetEnvironmentVariableW(name, value) != FALSE, "input deadline fixture configuration");
  }
  ~scoped_budget() {
    SetEnvironmentVariableW(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US", present_ ? previous_.c_str() : nullptr);
  }
  scoped_budget(const scoped_budget&) = delete;
  scoped_budget& operator=(const scoped_budget&) = delete;
 private:
  std::wstring previous_;
  bool present_{false};
};
inline void run_engine_configuration(const wchar_t* value, bool finite) {
  scoped_budget configuration(value);
  struct deadline_service final : detail::engine_input_service {
    std::atomic<bool> ready{true};
    bool legacy_called{false}, deadline_called{false};
    clock::time_point received{};
    bool pending() const noexcept override { return ready.load(); }
    void service() noexcept override { legacy_called = true; ready = false; }
    void service_before_until(std::uint64_t, clock::time_point deadline) noexcept override {
      received = deadline; deadline_called = true; ready = false;
    }
    void quiesce() noexcept override {}
    void close() noexcept override { ready = false; }
  };
  engine_thread engine;
  const auto before = clock::now();
  check(engine.start([] { return true; }), "deadline propagation engine startup");
  auto service = std::make_shared<deadline_service>();
  check(detail::engine_admission::register_input(engine, service), "deadline propagation registration");
  detail::engine_admission::notify_input(engine);
  const bool drained = engine.drain_for(std::chrono::seconds(5));
  const bool unregistered = engine.invoke([&] { detail::engine_admission::unregister_input(engine, service); });
  engine.stop();
  const bool deadline_matches = finite
      ? service->received >= before && service->received != (clock::time_point::max)()
      : service->received == (clock::time_point::max)();
  check(drained && unregistered && service->deadline_called && !service->legacy_called && deadline_matches,
      "actual Engine did not pass its configured shared deadline to input service");
}
#endif

inline void run_engine() {
#ifdef _WIN32
  run_engine_configuration(L"0", false);
  run_engine_configuration(L"1000", true);
#endif
}
} // namespace input_deadline_cases
