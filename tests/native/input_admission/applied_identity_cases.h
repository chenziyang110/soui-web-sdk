#pragma once
#include "../../../sdk/src/host/applied_input_identity.h"
#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <vector>
namespace applied_identity_cases {
using namespace soui::sdk;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline void run(engine_thread& engine) {
  std::mutex identity_mutex, control_mutex; std::condition_variable changed;
  interaction_generation published;
  detail::applied_input_identity::publish(identity_mutex, published, {1, 2, 3});
  check(detail::applied_input_identity::matches(identity_mutex, published, {1, 2, 3})
      && !detail::applied_input_identity::matches(identity_mutex, published, {1, 2, 4}), "unapplied geometry passed independent gate");
  bool locked = false, release = false;
  std::thread writer([&] {
    {
      std::unique_lock<std::mutex> identity(identity_mutex);
      std::unique_lock<std::mutex> control(control_mutex); locked = true; changed.notify_all();
      changed.wait(control, [&] { return release; });
    }
    detail::applied_input_identity::publish(identity_mutex, published, {1, 2, 4});
  });
  bool held = false;
  { std::unique_lock<std::mutex> lock(control_mutex); held = changed.wait_for(lock, std::chrono::seconds(5), [&] { return locked; }); }
  const bool contended = detail::applied_input_identity::matches(identity_mutex, published, {1, 2, 3});
  { std::lock_guard<std::mutex> lock(control_mutex); release = true; changed.notify_all(); }
  writer.join();
  check(held && !contended && detail::applied_input_identity::matches(identity_mutex, published, {1, 2, 4}), "UI did not decline contended publication without waiting");

  interaction_generation owner{1, 2, 3};
  std::vector<std::uint64_t> observed; observed.reserve(8);
  auto mailbox = std::make_shared<detail::input_mailbox>([&](const input_packet* value) {
    if (value) observed.push_back(input_generation_applies(value->kind, value->generation, owner) ? value->sequence : 999);
  }, [&engine](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
    return detail::engine_admission::commit_input(engine, head, pending, first, recovery);
  }, [&engine] { return detail::engine_admission::input_cutoff(engine); });
  struct independent final : independent_input_sink {
    std::mutex& mutex; const interaction_generation& published; unsigned direct{0};
    independent(std::mutex& mutex, const interaction_generation& published) : mutex(mutex), published(published) {}
    bool submit_wheel(const input_packet& packet, scroll_sample&) override {
      if (!detail::applied_input_identity::matches(mutex, published, packet.generation)) return false;
      ++direct; return true;
    }
    bool capture_scroll_barrier(const input_packet& packet, scroll_sample&) override {
      return detail::applied_input_identity::matches(mutex, published, packet.generation);
    }
  } direct(identity_mutex, published);
  struct sink final : engine_input_sink {
    std::shared_ptr<detail::input_mailbox> mailbox;
    explicit sink(std::shared_ptr<detail::input_mailbox> value) : mailbox(std::move(value)) {}
    bool post_input(const input_packet& packet) override { return mailbox->post(packet) == input_admission_result::accepted; }
  } fallback(mailbox);
  interaction_router router; route_result resize_wheel, document_wheel;
  check(detail::engine_admission::register_input(engine, mailbox), "applied identity mailbox registration");
  const bool setup = engine.invoke([&] {
    detail::applied_input_identity::publish(identity_mutex, published, owner);
    check(engine.post([&] {
      owner.geometry = 4; detail::applied_input_identity::publish(identity_mutex, published, owner); observed.push_back(20);
    }), "actual resize barrier admission");
    router.activate({1, 2, 4});
    input_packet wheel; wheel.kind = input_kind::wheel; wheel.generation = {1, 2, 4}; wheel.sequence = 21;
    resize_wheel = router.route(wheel, direct, fallback);
    check(engine.post([&] {
      owner.document = 3; detail::applied_input_identity::publish(identity_mutex, published, owner); observed.push_back(30);
    }), "actual document barrier admission");
    router.activate({1, 3, 4}); wheel.generation.document = 3; wheel.sequence = 31;
    document_wheel = router.route(wheel, direct, fallback);
  });
  detail::engine_admission::notify_input(engine);
  const bool drained = engine.drain_for(std::chrono::seconds(5));
  const bool closed = engine.invoke([&engine, mailbox] { detail::engine_admission::unregister_input(engine, mailbox); });
  if (!closed) engine.stop(false);
  check(setup && drained && closed && !direct.direct && resize_wheel.disposition == route_disposition::engine
      && document_wheel.disposition == route_disposition::engine && observed == std::vector<std::uint64_t>({20, 21, 30, 31}),
      "pending resize/document wheel bypassed FIFO or lost original fallback packet");
  detail::applied_input_identity::publish(identity_mutex, published, {});
  check(!detail::applied_input_identity::matches(identity_mutex, published, owner), "unknown/closed identity passed independent gate");
}
} // namespace applied_identity_cases
