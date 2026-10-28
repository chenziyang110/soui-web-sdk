// Private stage-0 resource experiment. No public SDK ABI is declared here.
#ifndef InspectorResourceSnapshot_h
#define InspectorResourceSnapshot_h

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace blink {

struct InspectorSnapshotCallbacks {
    unsigned int structSize, abiVersion;
    void* owner;
    void (__cdecl* retain)(void*);
    void (__cdecl* release)(void*);
    unsigned int (__cdecl* lookup)(void*, const char*, unsigned int,
        const unsigned char**, unsigned int*, unsigned int*);
};
struct InspectorSnapshotConfig {
    unsigned int structSize, abiVersion, fileCount, totalBytes;
    char payloadId[65];
    InspectorSnapshotCallbacks callbacks;
};
struct InspectorSnapshotReceipt {
    unsigned int structSize, abiVersion;
    char virtualBase[160];
};
struct InspectorResourceBytes {
    const unsigned char* data = nullptr;
    unsigned int size = 0, mime = 0;
};

// The shared identity is deliberately NOT an in-flight operation. Requests and
// the memory cache can retain it after completion without blocking broker drain.
class InspectorResourceState final : public std::enable_shared_from_this<InspectorResourceState> {
public:
    static std::shared_ptr<InspectorResourceState> create(
        const InspectorSnapshotConfig&, const char* nonce32);
    ~InspectorResourceState();
    static bool validRelativePath(const std::string&);
    static bool reservedURL(const std::string&);
    bool lookup(const std::string&, InspectorResourceBytes&);
    bool permits(const std::string&);
    bool arm() { bool expected = false; return !closed() && m_armed.compare_exchange_strong(expected, true); }
    const std::string& base() const { return m_base; }
    std::string entry() const { return m_base + "front_end/inspector.html"; }
    bool allowMainNavigation(const std::string&);
    void documentCommitted();
    void close() { m_closed.store(true); }
    bool closed() const { return m_closed.load(); }
    bool beginOperation();
    void endOperation(bool cancelled);
    unsigned int active() const { return m_active.load() + m_callbackActive.load(); }
    void enterCallback();
    void leaveCallback();
    bool post(std::function<void()>&&, bool terminal = false);
    bool finishCloseOnOwner();
    unsigned int queued() const;
    bool dispatcherAlive() const;
    void recordManagerEscape() { ++managerEscapes; close(); }

    std::atomic<unsigned long long> hits{0}, denied{0}, started{0}, completed{0},
        cancelled{0}, deliveredBytes{0}, managerEscapes{0}, unsupported{0};
private:
    struct Dispatcher;
    InspectorResourceState(const InspectorSnapshotConfig&, const char*);
    InspectorSnapshotCallbacks m_callbacks;
    std::shared_ptr<Dispatcher> m_dispatcher;
    std::string m_base;
    std::atomic<bool> m_closed{false}, m_committed{false}, m_armed{false};
    std::atomic<unsigned int> m_active{0};
    std::atomic<unsigned int> m_callbackActive{0};
};

// This preserves an already executing callout; it never grants a request,
// reads bytes, reopens admission, or replaces a missing actual Page identity.
class InspectorResourceCallbackBorrow final {
public:
    explicit InspectorResourceCallbackBorrow(const std::shared_ptr<InspectorResourceState>& state) : m_state(state)
    { if (m_state) m_state->enterCallback(); }
    ~InspectorResourceCallbackBorrow() { if (m_state) m_state->leaveCallback(); }
    InspectorResourceCallbackBorrow(const InspectorResourceCallbackBorrow&) = delete;
    InspectorResourceCallbackBorrow& operator=(const InspectorResourceCallbackBorrow&) = delete;
private:
    std::shared_ptr<InspectorResourceState> m_state;
};

} // namespace blink
#endif
