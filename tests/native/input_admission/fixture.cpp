#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include "ordinal_cases.h"
#include "terminal_cases.h"
#include "../../../sdk/src/host/cooperative_turn_budget.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace { std::atomic<bool> fail_next_allocation{false}; }
void* operator new(std::size_t bytes) {
  if (fail_next_allocation.exchange(false)) throw std::bad_alloc();
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

using namespace soui::sdk;
using detail::input_mailbox;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void drain(input_mailbox& mailbox) {
  for (unsigned count = 0; count < 1024 && mailbox.pending(); ++count) mailbox.service();
  require(!mailbox.pending(), "mailbox failed to reach idle");
}
input_packet packet(input_kind kind, std::uint64_t sequence) {
  input_packet value; value.kind = kind; value.sequence = sequence; return value;
}
int main() {
  try {
    namespace trace = soui::sdk::diagnostics::input_frame_trace;
    require(trace::process_recorder().configure(trace::current_identity(20261026, 2), trace::mode::detailed)
        && trace::register_current_thread(), "cold trace configuration");
    struct trace_detach { ~trace_detach() { trace::detach_current_thread(); } } trace_owner;
    require(input_generation_applies(input_kind::character, {1, 2, 3}, {1, 2, 4})
        && input_generation_applies(input_kind::ime_commit, {1, 2, 3}, {1, 2, 4})
        && input_generation_applies(input_kind::key_up, {1, 2, 3}, {1, 2, 4}), "geometry change discarded nonspatial edge");
    require(!input_generation_applies(input_kind::pointer_down, {1, 2, 3}, {1, 2, 4})
        && !input_generation_applies(input_kind::character, {1, 2, 3}, {1, 3, 4}), "spatial/document barrier bypassed");
    std::vector<std::uint64_t> observed;
    observed.reserve(256);
    input_mailbox mailbox([&](const input_packet* value) { observed.push_back(value ? value->sequence : 0); });
    for (unsigned index = 1; index <= 64; ++index)
      require(mailbox.post(packet(input_kind::pointer_move, index)) == input_admission_result::accepted, "ordinary quota");
    auto key = packet(input_kind::key_down, 65); key.key_code = 65;
    require(mailbox.post(key) == input_admission_result::accepted, "move flood stole key edge reserve");
    require(mailbox.take_wake_request() && !mailbox.take_wake_request(), "internal wake requests did not coalesce");
    auto commit = packet(input_kind::ime_commit, 66); commit.text = u"accepted text";
    require(mailbox.post(commit) == input_admission_result::accepted, "move flood stole IME commit reserve");
    require(mailbox.post(packet(input_kind::pointer_move, 67)) == input_admission_result::capacity, "full move quota not rejected");
    drain(mailbox);
    require(observed.size() == 67 && observed.front() == 1 && observed[65] == 66 && observed.back() == 0,
            "accepted inputs were discarded/reordered before cancellation");
    require(mailbox.post(key) == input_admission_result::recovering, "held key repeated after recovery");
    require(mailbox.post(packet(input_kind::character, 68)) == input_admission_result::recovering, "held key character leaked after recovery");
    key.kind = input_kind::key_up; key.sequence = 69;
    require(mailbox.post(key) == input_admission_result::accepted, "real key release cannot clear quarantine");
    key.kind = input_kind::key_down; key.sequence = 70;
    require(mailbox.post(key) == input_admission_result::accepted, "fresh key press did not recover");
    drain(mailbox);

    std::vector<std::uint64_t> edges;
    input_mailbox edge_flood([&](const input_packet* value) { edges.push_back(value ? value->sequence : 0); });
    for (unsigned index = 1; index <= 16; ++index)
      require(edge_flood.post(packet(input_kind::character, index)) == input_admission_result::accepted, "edge quota before16");
    require(edge_flood.post(packet(input_kind::key_up, 17)) == input_admission_result::capacity, "edge overflow not explicit");
    drain(edge_flood);
    require(edges.size() == 17 && edges[15] == 16 && edges[16] == 0, "critical overflow dropped accepted characters");

    input_mailbox bytes([](const input_packet*) {});
    auto text = packet(input_kind::ime_commit, 1); text.text.assign(32768, u'x');
    for (unsigned index = 0; index < 4; ++index)
      require(bytes.post(text) == input_admission_result::accepted, "bounded text item refused before byte cap");
    require(bytes.post(text) == input_admission_result::capacity, "aggregate byte bound not enforced");
    drain(bytes);
    input_mailbox allocation([](const input_packet*) {});
    fail_next_allocation.store(true);
    require(allocation.post(text) == input_admission_result::no_memory, "allocation rejection not typed");
    fail_next_allocation.store(false);
    drain(allocation);
    allocation.close();
    require(allocation.post(text) == input_admission_result::stopped, "closed mailbox accepted work");
    input_mailbox limited([](const input_packet*) {});
    auto blocked_key = packet(input_kind::key_down, 1); blocked_key.key_code = 65; blocked_key.generation = {1, 2, 3};
    limited.block_document(blocked_key.generation);
    require(limited.post(blocked_key) == input_admission_result::stopped, "unsupported native recovery resumed unknown state");
    blocked_key.kind = input_kind::key_up;
    require(limited.post(blocked_key) == input_admission_result::stopped, "blocked document claimed release delivered");
    blocked_key.kind = input_kind::key_down; blocked_key.generation.document = 3;
    require(limited.post(blocked_key) == input_admission_result::accepted, "new document did not recover after observed physical release");
    drain(limited);
    input_terminal_cases::run(fail_next_allocation);

    struct mock_service final : detail::engine_input_service {
      std::atomic<bool> ready{false}; std::atomic<unsigned> calls{0};
      bool pending() const noexcept override { return ready.load(); }
      void service() noexcept override { ++calls; ready = false; }
      void quiesce() noexcept override {}
      void close() noexcept override { ready = false; }
    };
    engine_thread engine;
    require(engine.start([] { return true; }), "engine startup");
    input_ordinal_cases::run(engine);
    trace::event context; context.input_first = context.input_last = 123; context.view_generation = 1;
    std::atomic<bool> inherited{false};
    {
      trace::context_scope scope(context);
      require(engine.post([&] {
        trace::event observed;
        inherited = trace::current_context(observed) && observed.task_id != 0 && observed.input_first == 123;
      }), "causal task post");
      require(engine.drain_for(std::chrono::seconds(5)) && inherited, "input context did not survive actual FIFO");
    }
    std::array<std::shared_ptr<mock_service>, 32> hosts;
    for (auto& host : hosts) { host = std::make_shared<mock_service>(); require(detail::engine_admission::register_input(engine, host), "host capacity before32"); }
    require(!detail::engine_admission::register_input(engine, std::make_shared<mock_service>()), "host registry exceeded32");
    for (auto& host : hosts) host->ready = true;
    detail::engine_admission::notify_input(engine);
    require(engine.drain_for(std::chrono::seconds(5)), "round-robin hosts did not drain");
    for (const auto& host : hosts) require(host->calls == 1, "a Host was starved or serviced twice");
    require(engine.invoke([&] { for (const auto& host : hosts) detail::engine_admission::unregister_input(engine, host); }), "owner unregister");
    engine.stop();
    const detail::cooperative_turn_budget::clock::time_point start{};
    const detail::cooperative_turn_budget shared(1000, start), legacy(0, start);
    const auto local = start + std::chrono::milliseconds(2);
    require(shared.limit(local) == start + std::chrono::milliseconds(1), "shared budget did not cap component deadline");
    require(legacy.limit(local) == local, "default policy changed independent budgets");
    require(detail::cooperative_turn_budget(99, start).limit(local) == local
        && detail::cooperative_turn_budget(5001, start).limit(local) == local, "out-of-range budget changed default");
    std::cout << "input mailbox source regression cases complete\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
