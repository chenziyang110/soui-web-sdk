#pragma once

#include "content/common/InputFrameTrace.h"

namespace content { namespace input_frame_trace {

// Use only at the known V8 QueueUserWorkItem callback boundary. This is not a
// general-purpose lazy registration hook for PostTask, record, or hot paths.
class background_producer_scope final {
 public:
  background_producer_scope() noexcept {
    if (current_thread_registered()) return;
    try {
      owns_registration_ = register_current_thread();
    } catch (...) {
      process_recorder().note_causal_loss();
      return;
    }
    if (!owns_registration_) process_recorder().note_causal_loss();
  }

  ~background_producer_scope() noexcept {
    if (owns_registration_) detach_current_thread();
  }

  background_producer_scope(const background_producer_scope&) = delete;
  background_producer_scope& operator=(const background_producer_scope&) = delete;
  background_producer_scope(background_producer_scope&&) = delete;
  background_producer_scope& operator=(background_producer_scope&&) = delete;

 private:
  bool owns_registration_{false};
};

}} // namespace content::input_frame_trace
