#ifndef SOUI_LOCAL_RESOURCE_STATE_H
#define SOUI_LOCAL_RESOURCE_STATE_H
#include "wke/LocalResourceApi.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace content {
class LocalResourceTask {
public:
    virtual ~LocalResourceTask() { }
    virtual void run() noexcept = 0;
    virtual void cancel() noexcept = 0;
};
struct LocalResourceBytes {
    const unsigned char* data = nullptr;
    unsigned int size = 0, mime = 0, status = 404;
};
// A cache/request may retain this identity after payload retirement. Such a
// reference is not an operation and must never hold the SDK snapshot alive.
class LocalResourceState final : public std::enable_shared_from_this<LocalResourceState> {
public:
    static std::shared_ptr<LocalResourceState> create(const SouiLocalResourceConfig&,
        const char* documentNonce, uint64_t viewBirth, uint64_t brokerDocument, uint64_t receipt,
        unsigned int* failure = nullptr);
    static std::shared_ptr<LocalResourceState> rejection();
    ~LocalResourceState();
    static bool validRelativePath(const std::string&);
    static bool reservedURL(const std::string&);
    static std::shared_ptr<LocalResourceState> forPage(const void* page);
    bool install(const void* page);
    bool permitsDocument(const std::string&, bool initialMainRequest) const;
    bool allowMainNavigation(const std::string&, bool redirect);
    bool permitsURL(const std::string&) const;
    bool lookup(const std::string&, LocalResourceBytes&);
    bool beginOperation(const std::shared_ptr<LocalResourceTask>&, unsigned int& slot);
    bool transferOperation(unsigned int slot, LocalResourceTask* reserved,
        const std::shared_ptr<LocalResourceTask>& replacement);
    bool borrowBytes(unsigned int);
    void endOperation(unsigned int slot, unsigned int bytes, bool cancelled);
    bool post(const std::shared_ptr<LocalResourceTask>&);
    void enterCallback();
    void leaveCallback();
    void close();
    bool finishClose();
    bool retired() const { return m_phase.load() == SouiLocalPayloadRetired; }
    bool closed() const { return m_phase.load() != SouiLocalReady; }
    bool failed() const { return m_failed.load(); }
    bool ownerThread() const;
    void fault();
    void snapshot(SouiLocalResourceDrain&) const;
    void receipt(SouiLocalResourceReceipt&) const;
    uint64_t id() const { return m_receipt; }
    const std::string& base() const { return m_base; }
    const std::string& entry() const { return m_entry; }
    void addDelivered(unsigned int bytes) { m_delivered.fetch_add(bytes); }
    // Enter/leave only at the actual Engine debug-loop boundaries. All
    // dispatchers observe the global V8 pause, including another target's.
    static void enterPause();
    static void leavePause();
    static bool paused();
#if defined(SOUI_LOCAL_RESOURCE_TESTING)
    // Unit boundary injection for the actual dispatcher failure branch. This
    // declaration and branch do not exist in the shipping kernel.
    static void failNextWakeForTesting();
#endif
private:
    struct Dispatcher;
    LocalResourceState();
    bool initializeDispatcher();
    void releaseBudget();
    SouiLocalResourceCallbacks m_callbacks = {};
    std::shared_ptr<Dispatcher> m_dispatcher;
    std::array<std::weak_ptr<LocalResourceTask>, SOUI_LOCAL_RESOURCE_MAX_OPERATIONS> m_operations;
    std::string m_base, m_entry;
    const void* m_page = nullptr; // comparison only, never dereferenced
    uint64_t m_viewBirth = 0, m_brokerDocument = 0, m_receipt = 0, m_closeSequence = 0;
    unsigned int m_ownerThread = 0, m_payloadBytes = 0;
    std::atomic<bool> m_budgetHeld{false};
    bool m_rejectionBudget = false, m_retiring = false,
        m_releaseFailed = false, m_navigationStarted = false;
    std::atomic<unsigned int> m_phase{SouiLocalReady}, m_active{0}, m_callbacksActive{0};
    std::atomic<bool> m_failed{false}, m_payloadHeld{false}, m_dispatchFailed{false};
    std::atomic<uint64_t> m_borrowedBytes{0}, m_started{0}, m_completed{0},
        m_cancelled{0}, m_denied{0}, m_delivered{0};
};
class LocalResourceCallbackBorrow final {
public:
    explicit LocalResourceCallbackBorrow(std::shared_ptr<LocalResourceState> state) : m_state(std::move(state))
    { m_state->enterCallback(); }
    ~LocalResourceCallbackBorrow() { m_state->leaveCallback(); }
    LocalResourceCallbackBorrow(const LocalResourceCallbackBorrow&) = delete;
    LocalResourceCallbackBorrow& operator=(const LocalResourceCallbackBorrow&) = delete;
private:
    std::shared_ptr<LocalResourceState> m_state;
};
}
#endif
