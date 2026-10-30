// Exact production scheduler/window sources, deterministic Win32 dependencies.
// This fixture source is delivered unexecuted.
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
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
bool failPost = false, failTimer = false, failCreateTimer = false;
bool crossThread = false, failNotify = false;
bool quitPending = false;
unsigned notifyAttempts = 0, closeAttempts = 0, drainAttempts = 0, precisionReferences = 0;
bool drainOutsideLock = true, closeAfterDrain = true, noLatePublication = true;
unsigned postAttempts = 0, timerCalls = 0, registrations = 0, unregistrations = 0, runs = 0, destroyed = 0;
LONGLONG dueTicks = 0;
HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
struct Timer { PTP_TIMER_CALLBACK callback; PVOID parameter; bool armed=false, drained=false; };
std::vector<Timer*> timers;
Timer* timer(PTP_TIMER value){return reinterpret_cast<Timer*>(value); }
BOOL WINAPI post(HWND w, UINT id, WPARAM a, LPARAM b) { ++postAttempts; if (failPost) return FALSE; messages.push_back({w,id,a,b}); return TRUE; }
DWORD WINAPI windowThread(HWND, LPDWORD) { return ::GetCurrentThreadId() + (crossThread ? 1 : 0); }
BOOL WINAPI notify(HWND w, UINT id, WPARAM a, LPARAM b) { ++notifyAttempts; if (failNotify) return FALSE; messages.push_back({w,id,a,b}); return TRUE; }
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
PTP_TIMER WINAPI createTimer(PTP_TIMER_CALLBACK callback,PVOID parameter,PTP_CALLBACK_ENVIRON) {
    ++registrations; if(failCreateTimer)return nullptr;
    auto* value=new Timer{callback,parameter};timers.push_back(value);return reinterpret_cast<PTP_TIMER>(value);
}
VOID WINAPI setPoolTimer(PTP_TIMER value,PFILETIME due,DWORD, DWORD) {
    auto* item=timer(value);item->armed=due!=nullptr;item->drained=false;
    if(due){ULARGE_INTEGER ticks;ticks.LowPart=due->dwLowDateTime;ticks.HighPart=due->dwHighDateTime;dueTicks=static_cast<LONGLONG>(ticks.QuadPart);}
}
VOID WINAPI waitPoolTimer(PTP_TIMER,BOOL);
VOID WINAPI closePoolTimer(PTP_TIMER value){
    ++closeAttempts;auto*item=timer(value);closeAfterDrain=closeAfterDrain&&item->drained;
    timers.erase(std::find(timers.begin(),timers.end(),item));delete item;
}
MMRESULT WINAPI beginPrecision(UINT){++precisionReferences;return TIMERR_NOERROR;}
MMRESULT WINAPI endPrecision(UINT){if(precisionReferences)--precisionReferences;else ++failures;return TIMERR_NOERROR;}
BOOL WINAPI peek(MSG*,HWND,UINT first,UINT last,UINT){return quitPending&&first==WM_QUIT&&last==WM_QUIT;}
void signal(PTP_TIMER value){auto*item=timer(value);if(item&&item->armed){item->armed=false;item->callback(nullptr,item->parameter,value);}}
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
    bool deferDelete=false;
    ~WebTimerBase(){++fixture::destroyed;}
    double repeatInterval()const{return 0;}
    void heapDelete();
    void setNextFireTime(double,void*){}
    void fired();
    void deref(){delete this;}
};
class WebThreadImpl {
public:
    unsigned m_threadId=1; int m_firingTimers=0;
    bool m_isMainThread=true,m_willExit=false,m_retiringCompletedTasks=false;
    HANDLE m_hEvent=nullptr;
    std::vector<WebTimerBase*> m_timerHeap;
    std::vector<WebTimerBase*> m_unusedTimersToDelete;
    task_scheduling::DeadlinePriorityIndex m_timerIndex;
    void schedulerTasks();void schedulerTasks(bool, unsigned long long = 0, unsigned long long = 0);void updateSharedTimer();
    bool serviceOwnerOpportunity(unsigned long long = 0, unsigned long long = 0);void enableScheduler();void retireCompletedTasksForOwner();
    void fire(unsigned long long deadlineQpc = 0, unsigned long long qpcFrequency = 0){schedulerTasks(true, deadlineQpc, qpcFrequency);} bool isCurrentThread()const{return true;}
    void deleteUnusedTimers(){} void startTriggerTasks(unsigned*,double,unsigned long long = 0,unsigned long long = 0,bool = false){}
    bool hasPendingTaskAdmission(){return false;}void willProcessTasks(){}void didProcessTasks(){}
    void add(double deadline=fixture::now,std::function<void()> callback={}){
        auto* timer = new WebTimerBase{this,deadline,200,std::move(callback),{}};
        m_timerIndex.prepare(200);
        timer->scheduling.value = timer; timer->scheduling.deadline = deadline; timer->scheduling.priority = 200;
        m_timerIndex.insert(timer->scheduling); m_timerHeap.push_back(timer);
        std::stable_sort(m_timerHeap.begin(),m_timerHeap.end(),[](const auto*a,const auto*b){return a->m_nextFireTime<b->m_nextFireTime;});
    }
    void clear(){for(auto*t:m_timerHeap){m_timerIndex.erase(t->scheduling);delete t;}m_timerHeap.clear();for(auto*t:m_unusedTimersToDelete)delete t;m_unusedTimersToDelete.clear();}
};
void WebTimerBase::fired(){++fixture::runs;fixture::now+=fixture::step;if(callback)callback();if(deferDelete)owner->m_unusedTimersToDelete.push_back(this);else delete this;}
void WebTimerBase::heapDelete(){owner->m_timerIndex.erase(scheduling);auto&heap=owner->m_timerHeap;heap.erase(std::find(heap.begin(),heap.end(),this));}
class CheckReEnter {public:static int getEnterCount(){return fixture::enters;}static void incrementEnterCount(){++fixture::enters;}static void decrementEnterCount(){--fixture::enters;}};
}
namespace blink {class Platform {public:content::WebThreadImpl*owner=nullptr;static Platform*current(){static Platform p;return&p;}content::WebThreadImpl*currentThread(){return owner;}};}
#undef PostMessage
#undef DefWindowProc
#define PostMessage fixture::post
#define PostMessageW fixture::post
#define PeekMessageW fixture::peek
#define SendNotifyMessageW fixture::notify
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
#define CreateThreadpoolTimer fixture::createTimer
#define SetThreadpoolTimer fixture::setPoolTimer
#define WaitForThreadpoolTimerCallbacks fixture::waitPoolTimer
#define CloseThreadpoolTimer fixture::closePoolTimer
#define timeBeginPeriod fixture::beginPrecision
#define timeEndPeriod fixture::endPrecision
#ifdef __clang__
#pragma clang diagnostic push
// The byte-verified legacy fragment uses Win32 aggregate zero initialization.
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "SharedTimerWin.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
namespace content {
#include "provider-cutoff.inc"
#include "scheduler.inc"
#include "enable.inc"
#include "retire.inc"
}
namespace fixture {
VOID WINAPI waitPoolTimer(PTP_TIMER value,BOOL){
    ++drainAttempts;auto*item=timer(value);
    const bool lockFree=::TryAcquireSRWLockExclusive(&content::continuationLock)!=FALSE;
    drainOutsideLock=drainOutsideLock&&lockFree;if(lockFree)::ReleaseSRWLockExclusive(&content::continuationLock);
    const auto before=messages.size();item->callback(nullptr,item->parameter,value);
    noLatePublication=noLatePublication&&messages.size()==before;item->armed=false;item->drained=true;
}
bool dispatchOne(){if(messages.empty())return false;auto m=messages.front();messages.pop_front();content::TimerWindowWndProc(m.window,m.id,m.first,m.second);return true;}
void drainMessages(){for(unsigned i=0;i<1024&&!messages.empty();++i)dispatchOne();expect(messages.empty(),"finite continuation chain");}
void reset(content::WebThreadImpl&thread){
    thread.clear();content::cancelSharedTimerDeadline();messages.clear();
    {content::SharedTimerContinuationLock lock;content::pendingContinuation=0;content::continuationDeferred=false;}
    now=100;step=0;queueStatus=0;enters=0;failPost=failTimer=failCreateTimer=failNotify=crossThread=quitPending=false;runs=destroyed=postAttempts=notifyAttempts=0;content::readyRetry=false;
}
int run(){
    content::WebThreadImpl thread;blink::Platform::current()->owner=&thread;
    thread.updateSharedTimer();expect(timerCalls==0&&content::armedDeadline==0,"idle owns no periodic timer");
    for(unsigned i=0;i<256;++i)thread.add();
    thread.schedulerTasks();expect(runs==32&&messages.size()==1,"ready batch yields at32 with coalesced wake");
    drainMessages();expect(runs==256&&destroyed==256,"ready work drains exactly once without clock advance");
    expect(content::armedDeadline==0,"ready drain becomes idle");
    reset(thread);thread.add();thread.m_timerHeap.back()->deferDelete=true;
    thread.schedulerTasks();expect(runs==1&&destroyed==1&&thread.m_unusedTimersToDelete.empty()&&messages.empty(),"last completed Task retires without another pulse");
    reset(thread);for(unsigned i=0;i<40;++i){auto*timer=new content::WebTimerBase{&thread,0,200,{},{}};thread.m_unusedTimersToDelete.push_back(timer);}
    thread.retireCompletedTasksForOwner();expect(destroyed==32&&thread.m_unusedTimersToDelete.size()==8&&!messages.empty(),"retirement is bounded and schedules remainder");
    drainMessages();expect(destroyed==40&&thread.m_unusedTimersToDelete.empty(),"retirement remainder progresses");
    reset(thread);thread.add(now+1.0/30.0);thread.schedulerTasks();
    expect(content::sharedHighResolution&&precisionReferences==1,"30Hz deadline requests balanced timer precision");
    reset(thread);thread.add(now+1.0/60.0);thread.schedulerTasks();
    expect(content::sharedHighResolution&&precisionReferences==1,"60Hz deadline requests balanced timer precision");
    reset(thread);thread.add(now+1.0/1000.0);thread.schedulerTasks();
    expect(content::sharedHighResolution&&precisionReferences==1,"1000Hz deadline requests balanced timer precision");
    reset(thread);thread.add(now+1.0);thread.schedulerTasks();
    expect(!content::sharedHighResolution&&precisionReferences==0,"distant deadline releases short-frame timer precision");
    reset(thread);
    thread.add(100.050);thread.schedulerTasks();
    expect(runs==0&&content::deadlineTicket&&dueTicks<0,"future arms relative one-shot");
    expect(content::deadlineTimer&&!content::timerID,"reusable pool timer avoids native fallback per arm");
    const auto oldTicket=content::deadlineTicket;
    signal(content::deadlineTimer);dispatchOne();
    expect(runs==0&&content::deadlineTicket!=oldTicket,"early callback rearms without running future work");
    content::TimerWindowWndProc(window,content::timerFiredMessage,oldTicket,0);
    expect(runs==0&&messages.empty(),"old queued ticket cannot consume rearm");
    now=100.051;signal(content::deadlineTimer);drainMessages();
    expect(runs==1&&content::armedDeadline==0,"deadline runs once then cancels timer");
    reset(thread);failTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::deadlineTimer&&!content::timerID,"pool timer remains armed while native timer fails");
    failPost=true;crossThread=true;now=101;signal(content::deadlineTimer);
    expect(notifyAttempts==1&&content::continuationDeferred&&!messages.empty(),"failed deadline post uses cross-thread notify with deferred receipt");
    failPost=false;drainMessages();expect(runs==1,"timer/native/post combination retains exactly-once progress");
    reset(thread);failTimer=failPost=failNotify=crossThread=true;thread.add(101);thread.schedulerTasks();now=101;
    signal(content::deadlineTimer);expect(messages.empty()&&content::continuationDeferred,"all delivery failures retain owner intent");
    failPost=failTimer=failNotify=crossThread=false;thread.serviceOwnerOpportunity();expect(runs==1,"owner opportunity recovers retained deadline intent");
    reset(thread);content::stopSharedTimer();failCreateTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::timerID>1001&&!content::deadlineTimer,"pool creation failure preserves ticketed native fallback");
    now=101;content::TimerWindowWndProc(window,WM_TIMER,content::timerID,0);expect(runs==1,"pool creation failure does not lose future task");
    reset(thread);content::stopSharedTimer();thread.updateSharedTimer();
    reset(thread);
    for(unsigned i=0;i<64;++i)thread.add();
    thread.schedulerTasks();queueStatus=QS_KEY<<16;dispatchOne();
    const auto attempts=postAttempts;
    for(unsigned i=0;i<64;++i)content::requestSharedTimerContinuation();
    expect(runs==32&&postAttempts==attempts&&content::continuationDeferred,"native pressure suppresses producer refill");
    queueStatus=0;thread.serviceOwnerOpportunity();drainMessages();
    expect(runs==64,"SDK owner opportunity resumes without waiting16ms");
    reset(thread);for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    quitPending=true;dispatchOne();expect(runs==32&&messages.empty(),"ready continuation leaves pending quit for the outer pump");
    quitPending=false;thread.serviceOwnerOpportunity();drainMessages();expect(runs==64,"resuming the owner retains accepted work");
    reset(thread);for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    enters=1;dispatchOne();expect(runs==32&&content::continuationDeferred,"nested callback defers");
    enters=0;thread.serviceOwnerOpportunity();drainMessages();expect(runs==64,"reentry recovery progresses");
    reset(thread);for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    thread.m_firingTimers=1;dispatchOne();expect(runs==32,"internal scheduler reentry is suppressed");
    thread.enableScheduler();drainMessages();expect(runs==64,"enabling scheduler rearms work after consumed continuation");
    reset(thread);failPost=true;for(unsigned i=0;i<64;++i)thread.add();thread.schedulerTasks();
    expect(messages.empty()&&content::deadlineTimer&&content::readyRetry,"post failure retains distinct ready retry intent");
    failPost=false;signal(content::retryTimer);drainMessages();
    expect(runs==64,"post failure does not lose ready tasks");
    reset(thread);thread.add(101);thread.schedulerTasks();
    failPost=true;thread.add();content::requestSharedTimerContinuation();
    failPost=false;signal(content::retryTimer);
    expect(!messages.empty()&&messages.front().id==content::continuationMessage,
        "ready retry is independent of an unrelated future deadline");
    drainMessages();expect(runs==1&&content::armedDeadline==101,
        "ready retry progresses now while retaining future deadline");
    reset(thread);thread.add(101);thread.schedulerTasks();const auto stale=content::deadlineTicket;
    content::stopSharedTimer();content::TimerWindowWndProc(window,WM_TIMER,stale,0);
    expect(runs==0&&!content::deadlineTimer&&!content::retryTimer&&drainOutsideLock&&closeAfterDrain&&noLatePublication,"stop drains callbacks outside publication lock before closing timers");
    content::initializeOffScreenTimerWindow();thread.updateSharedTimer();
    content::TimerWindowWndProc(window,WM_TIMER,stale,0);expect(runs==0,"same reused HWND rejects old native ticket");
    reset(thread);content::stopSharedTimer();failCreateTimer=failTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::deadlineTicket==0&&content::deadlineRetry,"total native scheduling failure retains truthful retry intent");
    failCreateTimer=failTimer=false;thread.serviceOwnerOpportunity();expect(content::deadlineTicket!=0&&runs==0,"actual owner opportunity retries future deadline without early execution");
    reset(thread);content::continuationSerial=(std::numeric_limits<UINT_PTR>::max)();
    expect(!content::requestSharedTimerContinuation(),"continuation serial never wraps");
    content::stopSharedTimer();
    expect(precisionReferences==0&&timers.empty(),"stop balances timer precision and releases both timer objects");
    std::printf("assertions=%d failures=%d source-only-delivery=true\n",assertions,failures);return failures?1:0;
}
}
int main(int argc,char**argv){
    if(argc==2){
        if(std::strcmp(argv[1],"--close-order")&&std::strcmp(argv[1],"--callback-drain"))return 2;
        content::WebThreadImpl thread;blink::Platform::current()->owner=&thread;thread.add(101);thread.schedulerTasks();
        content::stopSharedTimer();
        fixture::expect(fixture::closeAfterDrain&&fixture::drainAttempts==2&&fixture::closeAttempts==2,"both timer closes follow callback drain");
        fixture::expect(fixture::drainOutsideLock&&fixture::noLatePublication,"late callbacks run outside publication lock and cannot publish after stop");
        thread.clear();return fixture::failures?1:0;
    }
    return fixture::run();
}
