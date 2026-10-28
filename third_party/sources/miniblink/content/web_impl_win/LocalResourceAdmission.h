#ifndef SOUI_LOCAL_RESOURCE_ADMISSION_IMPL_H
#define SOUI_LOCAL_RESOURCE_ADMISSION_IMPL_H
#include "content/web_impl_win/LocalResourceState.h"
#include "platform/network/LocalResourceAdmission.h"
namespace blink {
class LocalResourceAdmission final : public content::LocalResourceTask,
    public std::enable_shared_from_this<LocalResourceAdmission> {
public:
    LocalResourceAdmission(const std::shared_ptr<content::LocalResourceState>&,
        const std::string& url, const std::string& method);
    ~LocalResourceAdmission();
    bool matches(const std::shared_ptr<content::LocalResourceState>&, const std::string&, const std::string&) const;
    bool reserve();
    bool transfer(const std::shared_ptr<content::LocalResourceTask>&,
        std::shared_ptr<content::LocalResourceState>&, unsigned int&);
    std::shared_ptr<content::LocalResourceState> owner() const { return m_identity.lock(); }
    void terminal();
    void claimLoader() { if (m_phase == Reserved) m_loaderOwned = true; }
    void abandonUnclaimed() { if (!m_loaderOwned) cancel(); }
    void run() noexcept override { cancel(); }
    void cancel() noexcept override;
private:
    enum Phase { Identified, Reserved, Transferred, Terminal };
    Phase m_phase = Identified;
    bool m_loaderOwned = false;
    std::weak_ptr<content::LocalResourceState> m_identity;
    std::shared_ptr<content::LocalResourceState> m_reservationOwner;
    std::string m_url, m_method;
    uint64_t m_receipt;
    unsigned int m_slot = 0;
};
}
#endif
