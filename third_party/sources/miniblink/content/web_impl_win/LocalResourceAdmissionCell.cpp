#include "content/web_impl_win/LocalResourceAdmission.h"
namespace blink {
LocalResourceAdmission::LocalResourceAdmission(const std::shared_ptr<content::LocalResourceState>& state,
    const std::string& url, const std::string& method)
    : m_identity(state), m_url(url), m_method(method), m_receipt(state->id()) { }
LocalResourceAdmission::~LocalResourceAdmission() { if (m_phase == Reserved) cancel(); }
bool LocalResourceAdmission::matches(const std::shared_ptr<content::LocalResourceState>& state,
    const std::string& url, const std::string& method) const {
    return m_phase != Terminal && state && state == m_identity.lock() && state->id() == m_receipt
        && !state->closed() && state->ownerThread() && m_method == "GET" && method == m_method
        && url == m_url && state->permitsURL(url);
}
bool LocalResourceAdmission::reserve() {
    auto state = m_identity.lock();
    if (!matches(state, m_url, m_method)) return false;
    if (m_phase == Reserved) return true;
    if (m_phase == Transferred) return false; // One actual loader consumes one cell.
    if (!state->beginOperation(shared_from_this(), m_slot)) return false;
    m_reservationOwner = state; m_phase = Reserved; return true;
}
bool LocalResourceAdmission::transfer(const std::shared_ptr<content::LocalResourceTask>& task,
    std::shared_ptr<content::LocalResourceState>& state, unsigned int& slot) {
    if (m_phase != Reserved || !m_reservationOwner
        || !m_reservationOwner->transferOperation(m_slot, this, task)) return false;
    state = m_reservationOwner; slot = m_slot;
    m_phase = Transferred; m_reservationOwner.reset(); return true;
}
void LocalResourceAdmission::cancel() noexcept {
    if (m_phase == Transferred || m_phase == Terminal) return;
    const bool reserved = m_phase == Reserved; m_phase = Terminal;
    auto owner = std::move(m_reservationOwner);
    if (reserved) owner->endOperation(m_slot, 0, true);
}
void LocalResourceAdmission::terminal() { if (m_phase == Transferred) m_phase = Terminal; else cancel(); }
}
