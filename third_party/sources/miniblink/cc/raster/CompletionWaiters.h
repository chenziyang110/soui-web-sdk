#ifndef CompletionWaiters_h
#define CompletionWaiters_h

#include <algorithm>
#include <mutex>
#include <new>
#include <vector>

namespace cc {

// A live host owns at most one registration. remove() joins any in-progress
// notification before its raw pointer can be destroyed. A notification may
// only enqueue owner work; it must not invoke external/page callbacks here.
template<typename Host, typename Allocator = std::allocator<Host*> >
class CompletionWaiters {
public:
    bool add(Host* host)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (std::find(m_hosts.begin(), m_hosts.end(), host) == m_hosts.end()) {
            try { m_hosts.push_back(host); }
            catch (const std::bad_alloc&) { return false; }
        }
        return true;
    }
    void remove(Host* host)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = std::find(m_hosts.begin(), m_hosts.end(), host);
        if (found != m_hosts.end())
            m_hosts.erase(found);
    }
    void notify()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (Host* host : m_hosts)
            host->notifyRecordResourcesChanged();
    }
private:
    std::mutex m_mutex;
    std::vector<Host*, Allocator> m_hosts;
};

} // namespace cc

#endif
