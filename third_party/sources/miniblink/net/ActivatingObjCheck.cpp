
#include "net/ActivatingObjCheck.h"
#include <vector>

namespace {

class CriticalSectionScope {
public:
    explicit CriticalSectionScope(CRITICAL_SECTION* mutex)
        : m_mutex(mutex)
    {
        ::EnterCriticalSection(m_mutex);
    }

    ~CriticalSectionScope() noexcept
    {
        ::LeaveCriticalSection(m_mutex);
    }

    CriticalSectionScope(const CriticalSectionScope&) = delete;
    CriticalSectionScope& operator=(const CriticalSectionScope&) = delete;

private:
    CRITICAL_SECTION* const m_mutex;
};

} // namespace

namespace net {

ActivatingObjCheck* ActivatingObjCheck::m_inst = nullptr;

ActivatingObjCheck::ActivatingObjCheck()
{
    m_newestId = 1;
    m_activatingObjs = new std::set<intptr_t>();
    ::InitializeCriticalSection(&m_mutex);
}

ActivatingObjCheck::~ActivatingObjCheck()
{
    ::DeleteCriticalSection(&m_mutex);
}

ActivatingObjCheck* ActivatingObjCheck::inst()
{
    if (!m_inst)
        m_inst = new ActivatingObjCheck();
    return m_inst;
}

void ActivatingObjCheck::destroy()
{
    delete m_activatingObjs;
    m_activatingObjs = nullptr;

    if (m_inst)
        delete m_inst;
}

void ActivatingObjCheck::add(intptr_t loader)
{
    const CriticalSectionScope lock(&m_mutex);
    m_activatingObjs->insert(loader);
}

void ActivatingObjCheck::remove(intptr_t loader)
{
    const CriticalSectionScope lock(&m_mutex);
    m_activatingObjs->erase(loader);
}

bool ActivatingObjCheck::isActivating(intptr_t loader)
{
    const CriticalSectionScope lock(&m_mutex);
    return m_activatingObjs->find(loader) != m_activatingObjs->end();
}

int ActivatingObjCheck::genId()
{
    InterlockedIncrement((long *)&m_newestId);
    return m_newestId;
}

void ActivatingObjCheck::testPrint()
{
#ifdef _DEBUG
    std::set<intptr_t>::iterator it = m_activatingObjs->begin();
    for (; it != m_activatingObjs->end(); ++it) {
        char* output = (char*)malloc(0x100);
        sprintf(output, "ActivatingObjCheck::testPrint %p\n", (void*)(*it));
        OutputDebugStringA(output);
        free(output);
    }
#endif
}

void ActivatingObjCheck::doGarbageCollected(bool forceGC)
{
    if (!forceGC) {
        const CriticalSectionScope lock(&m_mutex);
        int size = m_activatingObjs->size();
        if (size < 10)
            return;
    }

    std::vector<intptr_t> activatingLoaders;
    {
        const CriticalSectionScope lock(&m_mutex);
        activatingLoaders.resize(m_activatingObjs->size());

        std::set<intptr_t>::iterator setIt = m_activatingObjs->begin();
        std::set<intptr_t>::iterator end = m_activatingObjs->end();
        for (unsigned i = 0; setIt != end; ++setIt, ++i)
            activatingLoaders[i] = *setIt;
    }

//     std::vector<intptr_t>::iterator it = activatingLoaders.begin();
//     for (; it != activatingLoaders.end(); ++it) {
//         intptr_t loader = *it;
//         double time = WTF::currentTimeMS();
//         if (time - loader->startTime() > 10000 || forceGC) {
//             loader->onTimeout();
//         }
//     }
}

void ActivatingObjCheck::shutdown()
{
    doGarbageCollected(true);
}

} // net