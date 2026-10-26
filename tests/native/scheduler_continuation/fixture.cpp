// Exact production scheduler/window sources, deterministic Win32 dependencies.
// This fixture source is delivered unexecuted.
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "TaskSchedulingPolicy.h"
#include "TaskSchedulingIndex.h"
namespace fixture {
struct Message { HWND window; UINT id; WPARAM first; LPARAM second; };
std::deque<Message> messages;
double now = 100, step = 0;
DWORD queueStatus = 0;
int enters = 0, failures = 0, assertions = 0;
bool failPost = false, failTimer = false, failWait = false;
unsigned postAttempts = 0, timerCalls = 0, registrations = 0, unregistrations = 0, runs = 0, destroyed = 0;
LONGLONG dueTicks = 0;
HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
WAITORTIMERCALLBACK callback = nullptr;
PVOID callbackParameter = nullptr;
BOOL WINAPI post(HWND w, UINT id, WPARAM a, LPARAM b) { ++postAttempts; if (failPost) return FALSE; messages.push_back({w,id,a,b}); return TRUE; }
DWORD WINAPI windowThread(HWND, LPDWORD) { return ::GetCurrentThreadId(); }
DWORD WINAPI status(UINT flags) { return queueStatus & (flags | (flags << 16)); }
LRESULT WINAPI defaultProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
UINT_PTR WINAPI setTimer(HWND, UINT_PTR id, UINT, TIMERPROC) { ++timerCalls; return failTimer ? 0 : id; }
BOOL WINAPI killTimer(HWND, UINT_PTR) { return TRUE; }
BOOL WINAPI destroyWindow(HWND) { return TRUE; }
BOOL WINAPI unregisterClass(LPCWSTR, HINSTANCE) { return TRUE; }
HMODULE WINAPI module(LPCWSTR) { return nullptr; }
FARPROC WINAPI proc(HMODULE, LPCSTR) { return nullptr; } // Win7 compatible path.
ATOM WINAPI registerClass(const WNDCLASSEXW*) { return 1; }
HWND WINAPI createWindow(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID) { return window; }
UINT WINAPI registerMessage(LPCWSTR name) { return std::wstring(name).find(L"DeadlineReady") != std::wstring::npos ? WM_APP+101 : WM_APP+102; }
HANDLE WINAPI createWaitable(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(3)); }
BOOL WINAPI setWaitable(HANDLE,const LARGE_INTEGER* due,LONG,PTIMERAPCROUTINE,LPVOID,BOOL) { dueTicks=due->QuadPart; return TRUE; }
BOOL WINAPI cancelWaitable(HANDLE) { return TRUE; }
BOOL WINAPI closeHandle(HANDLE) { return TRUE; }
BOOL WINAPI registerWait(PHANDLE output,HANDLE,WAITORTIMERCALLBACK fn,PVOID param,ULONG,ULONG) {
    ++registrations; if (failWait) return FALSE;
    *output=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(4)); callback=fn; callbackParameter=param; return TRUE;
}
BOOL WINAPI unregisterWait(HANDLE,HANDLE) {
    ++unregistrations;
    // An in-flight callback is drained after its ticket was invalidated.
    if (callback) callback(callbackParameter,TRUE);
    callback=nullptr; callbackParameter=nullptr; return TRUE;
}
void expect(bool value,const char* text) { ++assertions; if(!value){++failures;std::printf("FAIL: %s\n",text);} }
}
namespace WTF { inline unsigned currentThread(){return 1;} }
inline double monotonicallyIncreasingTime(){return fixture::now;}
#define ASSERT(expression) ((void)0)
namespace content {
#include "budgets.inc"
class WebThreadImpl;
class WebTimerBase {
public:
    WebThreadImpl* owner; double m_nextFireTime; int m_priority=200; std::function<void()> callback;
    content::task_scheduling::DeadlinePriorityIndex::Entry scheduling;
    ~WebTimerBase(){++fixture::destroyed;}
    double repeatInterval()const{return 0;}
    void heapDelete();
    void setNextFireTime(double,void*){}
    void fired(){++fixture::runs;fixture::now+=fixture::step;if(callback)callback();delete this;}
};
class WebThreadImpl {
public:
    unsigned m_threadId=1; int m_firingTimers=0;
    bool m_isMainThread=true,m_willExit=false;
    std::vector<WebTimerBase*> m_timerHeap;
    task_scheduling::DeadlinePriorityIndex m_timerIndex;
    void schedulerTasks();void schedulerTasks(bool);void updateSharedTimer();void serviceOwnerOpportunity();void enableScheduler();
    void fire(){schedulerTasks();} bool isCurrentThread()const{return true;}
    void deleteUnusedTimers(){} void startTriggerTasks(unsigned*,double){}
    bool hasPendingTaskAdmission(){return false;}void willProcessTasks(){}void didProcessTasks(){}
    void add(double deadline=fixture::now,std::function<void()> callback={}){
        auto* timer = new WebTimerBase{this,deadline,200,std::move(callback),{}};
        m_timerIndex.prepare(200);
        timer->scheduling.value = timer; timer->scheduling.deadline = deadline; timer->scheduling.priority = 200;
        m_timerIndex.insert(timer->scheduling); m_timerHeap.push_back(timer);
        std::stable_sort(m_timerHeap.begin(),m_timerHeap.end(),[](const auto*a,const auto*b){return a->m_nextFireTime<b->m_nextFireTime;});
    }
    void clear(){for(auto*t:m_timerHeap){m_timerIndex.erase(t->scheduling);delete t;}m_timerHeap.clear();}
};
void WebTimerBase::heapDelete(){owner->m_timerIndex.erase(scheduling);auto&heap=owner->m_timerHeap;heap.erase(std::find(heap.begin(),heap.end(),this));}
class CheckReEnter {public:static int getEnterCount(){return fixture::enters;}static void incrementEnterCount(){++fixture::enters;}static void decrementEnterCount(){--fixture::enters;}};
}
namespace blink {class Platform {public:content::WebThreadImpl*owner=nullptr;static Platform*current(){static Platform p;return&p;}content::WebThreadImpl*currentThread(){return owner;}};}
#undef PostMessage
#undef DefWindowProc
#define PostMessage fixture::post
#define GetWindowThreadProcessId fixture::windowThread
#define GetQueueStatus fixture::status
#define DefWindowProc fixture::defaultProc
#define SetTimer fixture::setTimer
#define KillTimer fixture::killTimer
#define DestroyWindow fixture::destroyWindow
#define UnregisterClassW fixture::unregisterClass
#define GetModuleHandleW fixture::module
#define GetProcAddress fixture::proc
#define RegisterClassExW fixture::registerClass
#define CreateWindowExW fixture::createWindow
#define RegisterWindowMessageW fixture::registerMessage
#define CreateWaitableTimerW fixture::createWaitable
#define SetWaitableTimer fixture::setWaitable
#define CancelWaitableTimer fixture::cancelWaitable
#define RegisterWaitForSingleObject fixture::registerWait
#define UnregisterWaitEx fixture::unregisterWait
#define CloseHandle fixture::closeHandle
#include "SharedTimerWin.h"
namespace content {
#include "scheduler.inc"
#include "enable.inc"
}
namespace fixture {
bool dispatchOne(){if(messages.empty())return false;auto m=messages.front();messages.pop_front();content::TimerWindowWndProc(m.window,m.id,m.first,m.second);return true;}
void drainMessages(){for(unsigned i=0;i<1024&&!messages.empty();++i)dispatchOne();expect(messages.empty(),"finite continuation chain");}
void reset(content::WebThreadImpl&thread){
    thread.clear();content::cancelSharedTimerDeadline();messages.clear();
    {content::SharedTimerContinuationLock lock;content::pendingContinuation=0;content::continuationDeferred=false;}
    now=100;step=0;queueStatus=0;enters=0;failPost=failTimer=failWait=false;runs=destroyed=postAttempts=0;
}
int run(){
    content::WebThreadImpl thread;blink::Platform::current()->owner=&thread;
    thread.updateSharedTimer();expect(timerCalls==0&&content::armedDeadline==0,"idle owns no periodic timer");
    for(unsigned i=0;i<256;++i)thread.add();
    thread.schedulerTasks();expect(runs==32&&messages.size()==1,"ready batch yields at32 with coalesced wake");
    drainMessages();expect(runs==256&&destroyed==256,"ready work drains exactly once without clock advance");
    expect(content::armedDeadline==0,"ready drain becomes idle");
    reset(thread);
    thread.add(100.050);thread.schedulerTasks();
    expect(runs==0&&content::deadlineTicket&&dueTicks<0,"future arms relative one-shot");
    expect(content::timerID>1001,"native fallback ticket excludes reserved legacy IDs");
    auto oldCallback=callback;auto oldParameter=callbackParameter;auto oldTicket=content::deadlineTicket;
    oldCallback(oldParameter,TRUE);dispatchOne();
    expect(runs==0&&content::deadlineTicket!=oldTicket,"early callback rearms without running future work");
    oldCallback(oldParameter,TRUE);expect(messages.empty(),"old callback ticket cannot consume rearm");
    now=100.051;callback(callbackParameter,TRUE);drainMessages();
    expect(runs==1&&content::armedDeadline==0,"deadline runs once then cancels fallback");
    reset(thread);
    for(unsigned i=0;i<64;++i)thread.add();
    thread.schedulerTasks();queueStatus=QS_KEY<<16;dispatchOne();
    const auto attempts=postAttempts;
    for(unsigned i=0;i<64;++i)content::requestSharedTimerContinuation();
    expect(runs==32&&postAttempts==attempts&&content::continuationDeferred,"native pressure suppresses producer refill");
    queueStatus=0;thread.serviceOwnerOpportunity();drainMessages();
    expect(runs==64,"SDK owner opportunity resumes without waiting16ms");
    reset(thread);for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    enters=1;dispatchOne();expect(runs==32&&content::continuationDeferred,"nested callback defers");
    enters=0;thread.serviceOwnerOpportunity();drainMessages();expect(runs==64,"reentry recovery progresses");
    reset(thread);for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    thread.m_firingTimers=1;dispatchOne();expect(runs==32,"internal scheduler reentry is suppressed");
    thread.enableScheduler();drainMessages();expect(runs==64,"enabling scheduler rearms work after consumed continuation");
    reset(thread);failPost=true;for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    expect(messages.empty()&&content::timerID,"post failure retains a native deadline fallback");
    failPost=false;now=100.011;content::TimerWindowWndProc(window,WM_TIMER,content::timerID,0);drainMessages();
    expect(runs==64,"post failure does not lose ready tasks");
    reset(thread);thread.add(101);thread.schedulerTasks();const auto stale=content::deadlineTicket;
    content::stopSharedTimer();content::TimerWindowWndProc(window,WM_TIMER,stale,0);
    expect(runs==0&&!content::deadlineTimer&&!content::deadlineWait,"stop drains registered wait before closing handle");
    content::initializeOffScreenTimerWindow();thread.updateSharedTimer();
    content::TimerWindowWndProc(window,WM_TIMER,stale,0);expect(runs==0,"same reused HWND rejects old native ticket");
    reset(thread);failWait=failTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::deadlineTicket==0,"total native scheduling failure never claims armed wake");
    failWait=failTimer=false;thread.updateSharedTimer();expect(content::deadlineTicket!=0,"scheduling failure retries same deadline");
    reset(thread);content::continuationSerial=(std::numeric_limits<UINT_PTR>::max)();
    expect(!content::requestSharedTimerContinuation(),"continuation serial never wraps");
    content::stopSharedTimer();
    std::printf("assertions=%d failures=%d source-only-delivery=true\n",assertions,failures);return failures?1:0;
}
}
int main(){return fixture::run();}
