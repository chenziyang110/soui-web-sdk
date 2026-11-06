#pragma once

// Private diagnostic overlay only. Definitions are injected into the copied
// InputFrameTrace.cpp; the default vendor tree/DLL never includes this header.
#include "RenderPhaseCost.h"
#include <new>

namespace content { namespace phase_cost {
namespace model = soui::bench::render_phase;
struct clock final {
    model::u64 now_qpc() noexcept;
    model::u32 current_thread_id() noexcept;
};
using bank_type = model::bank<clock>;
bank_type& storage() noexcept;
bank_type* active_bank() noexcept;
std::size_t current_slot() noexcept;
bool request_mode(const char* value) noexcept;
void bind_trace(model::u32 pid, model::u64 creation, model::u64 run_high,
    model::u64 run_low, model::u64 frequency) noexcept;
void attach(std::size_t slot, model::u32 owner) noexcept;
void detach() noexcept;
void stop() noexcept;
const char* final_snapshot_json() noexcept;

class scope final {
public:
    explicit scope(model::metric kind) noexcept {
        if (auto* owner = active_bank()) {
            new (value_) primitive(*owner, current_slot(), kind);
            engaged_ = true;
        }
    }
    ~scope() { if (engaged_) value()->~primitive(); }
    scope(const scope&) = delete;
    scope& operator=(const scope&) = delete;
    scope(scope&&) = delete;
    scope& operator=(scope&&) = delete;
    bool finish(model::outcome result = model::outcome::returned) noexcept {
        return engaged_ && value()->finish(result);
    }
private:
    using primitive = bank_type::scope;
    primitive* value() noexcept { return reinterpret_cast<primitive*>(value_); }
    alignas(primitive) unsigned char value_[sizeof(primitive)];
    bool engaged_ = false;
};

template<class Now>
inline void observe_lateness(model::metric kind, double due, Now now) noexcept {
    if (auto* owner = active_bank())
        (void)owner->observe_lateness(current_slot(), kind, due, now);
}
}} // namespace content::phase_cost
