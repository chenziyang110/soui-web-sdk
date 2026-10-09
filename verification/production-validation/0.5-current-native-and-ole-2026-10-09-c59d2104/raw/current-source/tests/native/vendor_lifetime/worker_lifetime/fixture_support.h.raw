#pragma once
// Declared dependency surface only; worker/registry/timer ownership comes from pinned source.
#include <windows.h>
#include <process.h>
#include <mmsystem.h>
#include <atomic>
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace fixture {
extern DWORD engineThreadId;
extern std::atomic<bool> failScheduler;
void* allocateScheduler(std::size_t size);
void assertFailure(const char* expression, int line);
}
#if defined(NDEBUG)
#define ASSERT(expression) static_cast<void>(0)
#else
#define ASSERT(expression) ((expression) ? static_cast<void>(0) : fixture::assertFailure(#expression, __LINE__))
#endif
#define ASSERT_UNUSED(name, expression) ASSERT(expression)
#define RELEASE_ASSERT(expression) ((expression) ? static_cast<void>(0) : fixture::assertFailure(#expression, __LINE__))
#define WTF_MAKE_NONCOPYABLE(Type) Type(const Type&) = delete; Type& operator=(const Type&) = delete
#define WTF_MAKE_FAST_ALLOCATED(Type)

namespace blink {
using PlatformThreadId = std::uintptr_t;
struct WebTraceLocation { const char* function; const char* file; int line; };
class WebScheduler { public: virtual ~WebScheduler() = default; };
class WebThread {
public:
    class Task { public: virtual ~Task() = default; virtual void run() = 0; };
    class IdleTask { public: virtual ~IdleTask() = default; virtual void run(double) = 0; };
    class TaskObserver { public: virtual ~TaskObserver() = default; virtual void willProcessTask() = 0; virtual void didProcessTask() = 0; };
    virtual ~WebThread() = default;
    virtual void postTask(const WebTraceLocation&, Task*) = 0;
    virtual void postDelayedTask(const WebTraceLocation&, Task*, long long) = 0;
    virtual bool isCurrentThread() const = 0;
    virtual PlatformThreadId threadId() const = 0;
    virtual void addTaskObserver(TaskObserver*) = 0;
    virtual void removeTaskObserver(TaskObserver*) = 0;
    virtual WebScheduler* scheduler() const = 0;
};
class Platform {
public:
    virtual ~Platform() = default;
    virtual WebThread* currentThread() = 0;
    static Platform* current();
};
}
#define FROM_HERE blink::WebTraceLocation{__FUNCTION__, __FILE__, __LINE__}
namespace WTF {
using ThreadIdentifier = std::uint32_t;
inline ThreadIdentifier currentThread() { return ::GetCurrentThreadId(); }
inline bool isMainThread() { return ::GetCurrentThreadId() == fixture::engineThreadId; }
inline double currentTime() { LARGE_INTEGER n{}, f{}; ::QueryPerformanceCounter(&n); ::QueryPerformanceFrequency(&f); return static_cast<double>(n.QuadPart) / static_cast<double>(f.QuadPart); }
}
using WTF::ThreadIdentifier;
using WTF::currentThread;
using WTF::currentTime;
inline int atomicIncrement(volatile int* value) { return static_cast<int>(::InterlockedIncrement(reinterpret_cast<volatile LONG*>(value))); }
namespace base { inline void SetThreadName(const char*) { } }
namespace content {
class WebThreadImpl;
class WebSchedulerImpl : public blink::WebScheduler {
public:
    explicit WebSchedulerImpl(WebThreadImpl*);
    ~WebSchedulerImpl() override;
    static void* operator new(std::size_t size) { return fixture::allocateScheduler(size); }
    static void operator delete(void* pointer) noexcept { ::operator delete(pointer); }
private:
    WebThreadImpl* m_thread;
};
class CheckReEnter {
public:
    static void incrementEnterCount();
    static void decrementEnterCount();
    static int getEnterCount();
    static int s_kEnterCount;
};
class BlinkPlatformImpl : public blink::Platform {
public:
    // Projection of original platform declarations. This fixture supplies only setup,
    // never replacement registry methods, counter resets, or synthetic drain getters.
    void closeThread();
    blink::WebThread* createThread(const char* name);
    void onThreadExit(WebThreadImpl*);
    static void onCurrentThreadWhenWebThreadImplCreated(blink::WebThread*);
    blink::WebThread* currentThread() override;
    CRITICAL_SECTION* m_lock = nullptr;
    static const int m_maxThreadNum = 1000;
    std::vector<WebThreadImpl*> m_threads;
    int m_threadNum = 0;
    ThreadIdentifier m_mainThreadId = 0;
    blink::WebThread* m_mainThread = nullptr;
};
}
