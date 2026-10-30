// Exact production scheduler/window sources, deterministic Win32 dependencies.
// Real timer precision still requires a native runtime measurement.
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
unsigned preciseCreateAttempts = 0, preciseWaitCreates = 0, preciseArms = 0, preciseHandleCloses = 0;
bool preciseApiAvailable = true, failPreciseCreate = false, failPreciseWait = false, failPreciseArm = false;
bool preciseHandleAfterWait = true, windowAfterResources = true;
LONGLONG dueTicks = 0;
HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
struct Timer { PTP_TIMER_CALLBACK callback; PVOID parameter; bool armed=false, drained=false; };
std::vector<Timer*> timers;
struct PreciseTimer { bool armed=false; };
struct Wait { PTP_WAIT_CALLBACK callback; PVOID parameter; HANDLE target=nullptr, bound=nullptr; bool drained=false; };
std::vector<PreciseTimer*> preciseTimers;
std::vector<Wait*> waits;
Timer* timer(PTP_TIMER value){return reinterpret_cast<Timer*>(value); }
PreciseTimer* preciseTimer(HANDLE value){return reinterpret_cast<PreciseTimer*>(value);}
Wait* wait(PTP_WAIT value){return reinterpret_cast<Wait*>(value);}
BOOL WINAPI post(HWND w, UINT id, WPARAM a, LPARAM b) { ++postAttempts; if (failPost) return FALSE; messages.push_back({w,id,a,b}); return TRUE; }
DWORD WINAPI windowThread(HWND, LPDWORD) { return ::GetCurrentThreadId() + (crossThread ? 1 : 0); }
BOOL WINAPI notify(HWND w, UINT id, WPARAM a, LPARAM b) { ++notifyAttempts; if (failNotify) return FALSE; messages.push_back({w,id,a,b}); return TRUE; }
DWORD WINAPI status(UINT flags) { return queueStatus & (flags | (flags << 16)); }
LRESULT WINAPI defaultProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
UINT_PTR WINAPI setTimer(HWND, UINT_PTR id, UINT, TIMERPROC) { ++timerCalls; return failTimer ? 0 : id; }
BOOL WINAPI killTimer(HWND, UINT_PTR) { return TRUE; }
BOOL WINAPI destroyWindow(HWND) { windowAfterResources=windowAfterResources&&timers.empty()&&waits.empty()&&preciseTimers.empty();return TRUE; }
BOOL WINAPI unregisterClass(LPCWSTR, HINSTANCE) { return TRUE; }
HMODULE WINAPI module(LPCWSTR) { return nullptr; }
HANDLE WINAPI createPreciseTimer(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD flags, DWORD) {
    ++preciseCreateAttempts;
    if (flags != 0x00000002) ++failures;
    if(failPreciseCreate)return nullptr;
    auto* value=new PreciseTimer;preciseTimers.push_back(value);return reinterpret_cast<HANDLE>(value);
}
FARPROC WINAPI proc(HMODULE, LPCSTR name) {
    return preciseApiAvailable&&std::strcmp(name,"CreateWaitableTimerExW")==0
        ? reinterpret_cast<FARPROC>(createPreciseTimer) : nullptr;
}
BOOL WINAPI armPreciseTimer(HANDLE value,const LARGE_INTEGER* due,LONG,PTIMERAPCROUTINE,LPVOID,BOOL){
    ++preciseArms;if(failPreciseArm)return FALSE;
    preciseTimer(value)->armed=true;dueTicks=due->QuadPart;return TRUE;
}
BOOL WINAPI cancelPreciseTimer(HANDLE value){preciseTimer(value)->armed=false;return TRUE;}
BOOL WINAPI closePreciseTimer(HANDLE value){
    ++preciseHandleCloses;
    for(auto* item:waits)if(item->bound==value)preciseHandleAfterWait=false;
    auto* item=preciseTimer(value);preciseTimers.erase(std::find(preciseTimers.begin(),preciseTimers.end(),item));delete item;return TRUE;
}
PTP_WAIT WINAPI createPoolWait(PTP_WAIT_CALLBACK callback,PVOID parameter,PTP_CALLBACK_ENVIRON){
    ++preciseWaitCreates;if(failPreciseWait)return nullptr;
    auto* value=new Wait{callback,parameter};waits.push_back(value);return reinterpret_cast<PTP_WAIT>(value);
}
VOID WINAPI setPoolWait(PTP_WAIT value,HANDLE target,PFILETIME){
    auto* item=wait(value);item->target=target;
    if(target){item->bound=target;item->drained=false;}
}
VOID WINAPI waitPoolWait(PTP_WAIT,BOOL);
VOID WINAPI closePoolWait(PTP_WAIT value){
    ++closeAttempts;auto* item=wait(value);closeAfterDrain=closeAfterDrain&&item->drained;
    waits.erase(std::find(waits.begin(),waits.end(),item));delete item;
}
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
#define SetWaitableTimer fixture::armPreciseTimer
#define CancelWaitableTimer fixture::cancelPreciseTimer
#define CloseHandle fixture::closePreciseTimer
#define CreateThreadpoolWait fixture::createPoolWait
#define SetThreadpoolWait fixture::setPoolWait
#define WaitForThreadpoolWaitCallbacks fixture::waitPoolWait
#define CloseThreadpoolWait fixture::closePoolWait
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
VOID WINAPI waitPoolWait(PTP_WAIT value,BOOL){
    ++drainAttempts;auto* item=wait(value);
    const bool lockFree=::TryAcquireSRWLockExclusive(&content::continuationLock)!=FALSE;
    drainOutsideLock=drainOutsideLock&&lockFree;if(lockFree)::ReleaseSRWLockExclusive(&content::continuationLock);
    const auto before=messages.size();item->callback(nullptr,item->parameter,value,WAIT_OBJECT_0);
    noLatePublication=noLatePublication&&messages.size()==before;item->target=nullptr;item->drained=true;
}
VOID WINAPI waitPoolTimer(PTP_TIMER value,BOOL){
    ++drainAttempts;auto*item=timer(value);
    const bool lockFree=::TryAcquireSRWLockExclusive(&content::continuationLock)!=FALSE;
    drainOutsideLock=drainOutsideLock&&lockFree;if(lockFree)::ReleaseSRWLockExclusive(&content::continuationLock);
    const auto before=messages.size();item->callback(nullptr,item->parameter,value);
    noLatePublication=noLatePublication&&messages.size()==before;item->armed=false;item->drained=true;
}
void signalDeadline(){
    if(content::preciseDeadlineWait&&wait(content::preciseDeadlineWait)->target){
        auto* item=wait(content::preciseDeadlineWait);
        preciseTimer(item->target)->armed=false;item->target=nullptr;
        item->callback(nullptr,item->parameter,content::preciseDeadlineWait,WAIT_OBJECT_0);
    }else signal(content::deadlineTimer);
}
bool dispatchOne(){if(messages.empty())return false;auto m=messages.front();messages.pop_front();content::TimerWindowWndProc(m.window,m.id,m.first,m.second);return true;}
void drainMessages(){for(unsigned i=0;i<1024&&!messages.empty();++i)dispatchOne();expect(messages.empty(),"finite continuation chain");}
void reset(content::WebThreadImpl&thread){
    thread.clear();content::cancelSharedTimerDeadline();messages.clear();
    {content::SharedTimerContinuationLock lock;content::pendingContinuation=0;content::continuationDeferred=false;}
    now=100;step=0;queueStatus=0;enters=0;failPost=failTimer=failCreateTimer=failNotify=crossThread=quitPending=false;
    failPreciseCreate=failPreciseWait=failPreciseArm=false;preciseApiAvailable=true;
    runs=destroyed=postAttempts=notifyAttempts=0;content::readyRetry=false;
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
    expect(preciseCreateAttempts>0,"future deadline first probes high-resolution waitable timer support");
    expect(content::preciseDeadlineTimer&&content::preciseDeadlineWait&&!content::sharedHighResolution&&precisionReferences==0,
        "supported high-resolution deadline does not rely on process timer precision");
    const auto reusableHandle=content::preciseDeadlineTimer;
    const auto reusableWait=content::preciseDeadlineWait;
    const auto createsBeforeRearm=preciseCreateAttempts,waitCreatesBeforeRearm=preciseWaitCreates,drainsBeforeRearm=drainAttempts;
    reset(thread);thread.add(now+1.0/60.0);thread.schedulerTasks();
    expect(content::preciseDeadlineTimer==reusableHandle&&content::preciseDeadlineWait==reusableWait,
        "60Hz deadline reuses high-resolution timer and wait");
    reset(thread);thread.add(now+1.0/1000.0);thread.schedulerTasks();
    expect(preciseTimer(content::preciseDeadlineTimer)->armed&&wait(content::preciseDeadlineWait)->target==content::preciseDeadlineTimer,
        "1000Hz deadline arms the high-resolution waitable path");
    expect(preciseCreateAttempts==createsBeforeRearm&&preciseWaitCreates==waitCreatesBeforeRearm&&drainAttempts==drainsBeforeRearm,
        "rearming does not create resources or synchronously drain callbacks");
    reset(thread);thread.add(now+1.0);thread.schedulerTasks();
    expect(!content::sharedHighResolution&&precisionReferences==0,"distant deadline releases short-frame timer precision");
    reset(thread);
    thread.add(100.050);thread.schedulerTasks();
    expect(runs==0&&content::deadlineTicket&&dueTicks<0,"future arms relative one-shot");
    expect(content::preciseDeadlineWait&&!timer(content::deadlineTimer)->armed&&!content::timerID,
        "high-resolution path is preferred over ordinary pool and native timers");
    const auto oldTicket=content::deadlineTicket;
    signalDeadline();dispatchOne();
    expect(runs==0&&content::deadlineTicket!=oldTicket,"early callback rearms without running future work");
    content::TimerWindowWndProc(window,content::timerFiredMessage,oldTicket,0);
    expect(runs==0&&messages.empty(),"old queued ticket cannot consume rearm");
    wait(content::preciseDeadlineWait)->callback(nullptr,nullptr,content::preciseDeadlineWait,WAIT_OBJECT_0);
    dispatchOne();expect(runs==0&&content::armedDeadline==100.050,
        "late callback from a cancelled precise wait cannot run the current future deadline early");
    now=100.051;signalDeadline();drainMessages();
    expect(runs==1&&content::armedDeadline==0,"deadline runs once then cancels timer");
    expect(!preciseTimer(content::preciseDeadlineTimer)->armed&&!wait(content::preciseDeadlineWait)->target,
        "completed deadline disarms both retained precise resources");
    reset(thread);failTimer=true;thread.add(101);thread.schedulerTasks();
    expect(wait(content::preciseDeadlineWait)->target==content::preciseDeadlineTimer&&!content::timerID,
        "precise deadline remains armed while native timer fails");
    failPost=true;crossThread=true;now=101;signalDeadline();
    expect(notifyAttempts==1&&content::continuationDeferred&&!messages.empty(),"failed deadline post uses cross-thread notify with deferred receipt");
    failPost=false;drainMessages();expect(runs==1,"timer/native/post combination retains exactly-once progress");
    reset(thread);failTimer=failPost=failNotify=crossThread=true;thread.add(101);thread.schedulerTasks();now=101;
    signalDeadline();expect(messages.empty()&&content::continuationDeferred,"all delivery failures retain owner intent");
    failPost=failTimer=failNotify=crossThread=false;thread.serviceOwnerOpportunity();expect(runs==1,"owner opportunity recovers retained deadline intent");
    reset(thread);content::stopSharedTimer();failPreciseCreate=failCreateTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::timerID>1001&&!content::deadlineTimer,"pool creation failure preserves ticketed native fallback");
    now=101;content::TimerWindowWndProc(window,WM_TIMER,content::timerID,0);expect(runs==1,"pool creation failure does not lose future task");
    reset(thread);content::stopSharedTimer();thread.updateSharedTimer();
    reset(thread);failPreciseArm=true;thread.add(now+1.0/60.0);thread.schedulerTasks();
    expect(content::preciseDeadlineTimer&&content::preciseDeadlineWait&&!wait(content::preciseDeadlineWait)->target
        &&timer(content::deadlineTimer)->armed&&!content::timerID&&content::sharedHighResolution&&precisionReferences==1,
        "failed precise arm uses retained pool timer with balanced compatibility precision");
    now+=1.0/60.0;signalDeadline();drainMessages();expect(runs==1&&precisionReferences==0,"failed precise arm fallback progresses and releases precision");
    reset(thread);content::stopSharedTimer();failPreciseWait=true;thread.add(101);thread.schedulerTasks();
    expect(!content::preciseDeadlineTimer&&!content::preciseDeadlineWait&&preciseTimers.empty()&&waits.empty()
        &&timer(content::deadlineTimer)->armed,"failed wait creation closes unused precise timer and falls back to pool timer");
    now=101;signalDeadline();drainMessages();expect(runs==1,"wait creation fallback does not lose future task");
    reset(thread);content::stopSharedTimer();failPreciseArm=failCreateTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::preciseDeadlineWait&&!wait(content::preciseDeadlineWait)->target&&!content::deadlineTimer&&content::timerID>1001,
        "failed precise arm and pool creation keep ticketed native timer fallback");
    now=101;content::TimerWindowWndProc(window,WM_TIMER,content::timerID,0);
    expect(runs==1,"precise arm plus pool creation failure does not lose future task");
    reset(thread);content::stopSharedTimer();preciseApiAvailable=false;const auto attemptsBeforeUnavailable=preciseCreateAttempts;
    thread.add(now+1.0/30.0);thread.schedulerTasks();
    expect(preciseCreateAttempts==attemptsBeforeUnavailable&&!content::preciseDeadlineTimer&&timer(content::deadlineTimer)->armed
        &&content::sharedHighResolution&&precisionReferences==1,"unavailable precise API keeps Win7-compatible pool timer and short-deadline precision");
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
    expect(runs==0&&!content::preciseDeadlineTimer&&!content::preciseDeadlineWait&&!content::deadlineTimer&&!content::retryTimer
        &&drainOutsideLock&&closeAfterDrain&&noLatePublication&&preciseHandleAfterWait&&windowAfterResources,
        "stop drains callbacks outside publication lock before closing wait, handle, timers and window");
    content::initializeOffScreenTimerWindow();thread.updateSharedTimer();
    content::TimerWindowWndProc(window,WM_TIMER,stale,0);expect(runs==0,"same reused HWND rejects old native ticket");
    reset(thread);content::stopSharedTimer();failPreciseCreate=failCreateTimer=failTimer=true;thread.add(101);thread.schedulerTasks();
    expect(content::deadlineTicket==0&&content::deadlineRetry,"total native scheduling failure retains truthful retry intent");
    failPreciseCreate=failCreateTimer=failTimer=false;thread.serviceOwnerOpportunity();expect(content::deadlineTicket!=0&&runs==0,"actual owner opportunity retries future deadline without early execution");
    reset(thread);content::continuationSerial=(std::numeric_limits<UINT_PTR>::max)();
    expect(!content::requestSharedTimerContinuation(),"continuation serial never wraps");
    content::stopSharedTimer();
    expect(precisionReferences==0&&timers.empty()&&waits.empty()&&preciseTimers.empty(),"stop balances timer precision and releases all timer and wait objects");
    std::printf("assertions=%d failures=%d source-only-delivery=true\n",assertions,failures);return failures?1:0;
}
}
int main(int argc,char**argv){
    if(argc==2){
        if(std::strcmp(argv[1],"--close-order")&&std::strcmp(argv[1],"--callback-drain"))return 2;
        content::WebThreadImpl thread;blink::Platform::current()->owner=&thread;thread.add(101);thread.schedulerTasks();
        content::stopSharedTimer();
        fixture::expect(fixture::closeAfterDrain&&fixture::drainAttempts==3&&fixture::closeAttempts==3
            &&fixture::preciseHandleCloses==1&&fixture::preciseHandleAfterWait&&fixture::windowAfterResources,
            "timer and wait closes follow callback drain before precise handle and window close");
        fixture::expect(fixture::drainOutsideLock&&fixture::noLatePublication,"late callbacks run outside publication lock and cannot publish after stop");
        thread.clear();return fixture::failures?1:0;
    }
    return fixture::run();
}
