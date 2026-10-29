#include <soui/host/interaction_protocol.h>

namespace soui::sdk {

void interaction_router::activate(interaction_generation generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  generation_ = generation;
  last_sequence_ = 0;
  visual_sample_ = {};
  active_ = true;
}

void interaction_router::deactivate() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = false;
}

route_result interaction_router::route(const input_packet& packet,
                                        independent_input_sink& independent,
                                        engine_input_sink& engine) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_) return {route_disposition::rejected_inactive, last_sequence_, visual_sample_};
  if (!input_generation_applies(packet.kind, packet.generation, generation_))
    return {route_disposition::rejected_stale, last_sequence_, visual_sample_};
  if (packet.sequence == 0 || packet.sequence <= last_sequence_)
    return {route_disposition::rejected_out_of_order, last_sequence_, visual_sample_};

  last_sequence_ = packet.sequence;
  if (packet.kind == input_kind::wheel) {
    scroll_sample sample = visual_sample_;
    if (independent.submit_wheel(packet, sample)) {
      visual_sample_ = sample;
      return {route_disposition::independent, packet.sequence, visual_sample_, input_admission_result::accepted};
    }
  }

  input_packet engine_packet;
  try { engine_packet = packet; }
  catch (...) {
    // Allocation failure also seals the mailbox segment. No partially copied
    // character/commit is reported accepted and the marker needs no text copy.
    input_packet cancellation;
    cancellation.kind = input_kind::cancel;
    cancellation.generation = packet.generation;
    // Internal recovery owns no second terminal for the rejected packet.
    cancellation.sequence = 0;
    (void)engine.post_input_typed(cancellation);
    return {route_disposition::rejected_sink, 0, visual_sample_, input_admission_result::no_memory};
  }
  // The press captures one immutable visual-state barrier. The release stays
  // behind it in the engine FIFO, so taking a second barrier would let a newer
  // scene split one physical click into two different hit-test states.
  if (packet.kind == input_kind::pointer_down) {
    engine_packet.barrier = {};
    scroll_sample captured{};
    if (independent.capture_scroll_barrier(packet, captured)) {
      visual_sample_ = captured;
      engine_packet.barrier = captured;
    }
  }
  const auto admission = engine.post_input_typed(engine_packet);
  if (admission != input_admission_result::accepted && admission != input_admission_result::coalesced)
    return {route_disposition::rejected_sink, 0, visual_sample_, admission};
  return {route_disposition::engine, packet.sequence, visual_sample_, admission};
}

interaction_generation interaction_router::generation() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

std::uint64_t interaction_router::last_sequence() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_sequence_;
}

}  // namespace soui::sdk
