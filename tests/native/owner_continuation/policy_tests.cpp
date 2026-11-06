#include "../../../sdk/src/host/owner_continuation_policy.h"
#include <atomic>
#include <cstdio>
#include <thread>

using namespace soui::sdk::detail::owner_loop_policy;
namespace {
constexpr UINT legacy_message=WM_APP+0x360, sent_message=WM_APP+0x361, control_message=WM_APP+0x362;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct event {
  HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  event(){require(value!=nullptr,"event");} ~event(){CloseHandle(value);}
  void set(){require(SetEvent(value)!=FALSE,"SetEvent");}
  void reset(){require(ResetEvent(value)!=FALSE,"ResetEvent");}
  void wait(){require(WaitForSingleObject(value,1000)==WAIT_OBJECT_0,"bounded event wait");}
};
struct model;
struct host {
  HWND window{}; std::function<void()> sent; unsigned controls{}; bool failed{};
  static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l);
  host(){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=proc;c.lpszClassName=L"W02PrototypeHiddenFixture";
    RegisterClassW(&c);window=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,c.hInstance,this);require(window!=nullptr,"hidden host");}
  ~host(){DestroyWindow(window);}
};
struct model {
  dispatcher& d; host& h; std::shared_ptr<int> lifetime=std::make_shared<int>(1); ticket t;
  std::atomic<int> work{0},calls{0},notifies{0}; std::function<void()> after_read; bool throw_once{};
  model(dispatcher& owner,host& target):d(owner),h(target){t=d.attach(lifetime,[this]{return pump();},[this]{++notifies;require(PostMessageW(h.window,legacy_message,0,reinterpret_cast<LPARAM>(this))!=FALSE,"legacy post");});}
  bool pump(){++calls;
    if(work.load()>0)--work;const bool more=work.load()>0;auto callback=after_read;if(callback)callback();
    if(throw_once){throw_once=false;throw std::runtime_error("pump boundary");}return more;}
  void legacy(){if(t.attached())t.legacy_turn();else{const bool more=pump();if(more){++notifies;PostMessageW(h.window,legacy_message,0,reinterpret_cast<LPARAM>(this));}}}
  void add(int n){work+=n;t.request();}
};
LRESULT CALLBACK host::proc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));return TRUE;}
  auto self=reinterpret_cast<host*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  try{if(self&&m==legacy_message){reinterpret_cast<model*>(l)->legacy();return 0;}if(self&&m==sent_message){if(self->sent)self->sent();return 0;}if(self&&m==control_message){++self->controls;return 0;}}
  catch(...){if(self)self->failed=true;return 0;}return DefWindowProcW(h,m,w,l);
}
MSG take(HWND hwnd,UINT type){MSG m{};require(PeekMessageW(&m,hwnd,type,type,PM_REMOVE)!=FALSE,"expected real queued message");return m;}
template<class F>void nested_until(F done){const auto deadline=GetTickCount64()+1500;
  while(!done()){require(GetTickCount64()<deadline,"nested loop deadline");MSG m{};
    if(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){require(m.message!=WM_QUIT,"unexpected quit");TranslateMessage(&m);DispatchMessageW(&m);}
    else MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}}
void drain(model& m){nested_until([&]{return m.work.load()==0&&(!m.t.attached()||!m.t.pending());});require(!m.h.failed,"legacy callback failed");
  MSG x{};while(PeekMessageW(&x,m.h.window,legacy_message,legacy_message,PM_REMOVE))DispatchMessageW(&x);}
void cleanup(dispatcher& d,model& m){drain(m);d.detach(m.t);MSG x{};while(PeekMessageW(&x,d.window(),poke_message,poke_message,PM_REMOVE))d.consume_root(x);}
UINT_PTR WINAPI failed_timer(HWND,UINT_PTR,UINT,TIMERPROC){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
BOOL WINAPI failed_post(HWND,UINT,WPARAM,LPARAM){SetLastError(ERROR_NOT_ENOUGH_QUOTA);return FALSE;}
UINT_PTR WINAPI different_timer_return(HWND h,UINT_PTR id,UINT ms,TIMERPROC proc){const auto r=SetTimer(h,id,ms,proc);return r?(id==1?2:1):0;}
BOOL WINAPI failed_cancel(HWND,UINT_PTR){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
MSG wait_timer(HWND window){const auto deadline=GetTickCount64()+1500;MSG m{};while(GetTickCount64()<deadline){if(PeekMessageW(&m,window,WM_TIMER,WM_TIMER,PM_REMOVE))return m;
  MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_TIMER,MWMO_INPUTAVAILABLE);}throw std::runtime_error("actual timer deadline");}
unsigned cases{};
template<class F>void test(const char* name,F body){body();++cases;std::printf("PASS %s\n",name);}
}
int run_owner_policy_contracts(){try{
 test("root-backlog-needs-no-self-post-or-timer-turn",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(64);d.consume_root(take(d.window(),poke_message));
   while(d.service_turn()){}require(m.calls==64&&!m.t.pending(),"root progress without timer dispatch");MSG x{};require(!PeekMessageW(&x,d.window(),poke_message,poke_message,PM_REMOVE),"root self-post chain");cleanup(d,m);});
 test("idle-does-not-ack-queued-notification",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(1);d.service_turn();m.add(1);auto first=take(d.window(),poke_message);
   MSG extra{};require(!PeekMessageW(&extra,d.window(),poke_message,poke_message,PM_REMOVE),"duplicate queued poke");d.consume_root(first);require(m.calls==2,"old queued responsibility retained");cleanup(d,m);});
 test("zero-token-cannot-consume-or-fallback-current-registration",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(2);auto fake=take(d.window(),poke_message);d.consume_root(fake);fake.wParam=0;
   d.consume_root(fake);require(m.calls==1&&m.t.pending(),"zero token admitted by root matcher");const auto n=m.notifies.load();DispatchMessageW(&fake);require(m.notifies==n,"zero token admitted by WndProc matcher");while(d.service_turn()){}cleanup(d,m);});
 test("worker-during-false-callback-preserves-pending",[]{dispatcher d;host h;d.begin_root();model m(d,h);event enter,done;
   std::thread worker([&]{enter.wait();m.add(1);done.set();});m.after_read=[&]{m.after_read={};enter.set();done.wait();};m.add(1);
   try{d.consume_root(take(d.window(),poke_message));require(m.t.pending(),"worker request lost after false");worker.join();while(d.service_turn()){}require(m.calls==2,"worker item not serviced");cleanup(d,m);}catch(...){worker.join();throw;}});
 test("worker-between-root-slices-does-not-restart-posted-chain",[]{dispatcher d;host h;d.begin_root();model m(d,h);event step,done;std::atomic<bool> cancel{false};m.add(3);d.consume_root(take(d.window(),poke_message));
   std::thread worker([&]{for(unsigned i=0;i<3;++i){step.wait();step.reset();if(cancel.load())break;m.add(1);done.set();}});
   try{for(unsigned i=0;i<3;++i){done.reset();step.set();done.wait();MSG unexpected{};require(!PeekMessageW(&unexpected,d.window(),poke_message,poke_message,PM_REMOVE),"active pending worker restarted self-post chain");require(d.service_turn(),"root continuation lost worker work");}worker.join();while(d.service_turn()){}cleanup(d,m);}
   catch(...){cancel=true;step.set();worker.join();throw;}});
 test("fallback-poke-before-real-nested-dispatch",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(3);d.consume_root(take(d.window(),poke_message));d.before_external_callback();
   nested_until([&]{return m.notifies.load()>0;});require(m.calls==1,"nested private WndProc must delegate legacy");cleanup(d,m);});
 test("first-pump-callback-modal-has-safety-not-reentrant-pump",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.after_read=[&]{m.after_read={};nested_until([&]{return m.notifies.load()>0;});require(m.calls==1,"nested callback reentered pump");};
   m.add(2);d.consume_root(take(d.window(),poke_message));cleanup(d,m);});
 test("sent-handler-nested-loop-reached-by-safety-timer",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(3);d.consume_root(take(d.window(),poke_message));event entered;
   bool sent_done=false;h.sent=[&]{nested_until([&]{return m.notifies.load()>0;});sent_done=true;};
   std::thread sender([&]{entered.set();DWORD_PTR ignored{};SendMessageTimeoutW(h.window,sent_message,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1200,&ignored);});entered.wait();
   try{const auto deadline=GetTickCount64()+1500;while(!sent_done){require(GetTickCount64()<deadline,"sent dispatch deadline");MSG msg{};const auto r=GetMessageW(&msg,nullptr,0,0);require(r>0,"GetMessage");if(!d.consume_root(msg))DispatchMessageW(&msg);}sender.join();cleanup(d,m);}catch(...){sender.join();throw;}});
 test("dequeued-old-poke-cannot-ack-new-registration",[]{dispatcher d;host h;d.begin_root();model a(d,h);a.add(1);auto stale=take(d.window(),poke_message);d.detach(a.t);model b(d,h);b.add(1);auto current=take(d.window(),poke_message);
   require(stale.lParam!=current.lParam&&stale.wParam!=current.wParam,"incarnation/token reused");d.consume_root(stale);require(b.calls==0&&b.t.pending(),"old MSG acknowledged new work");d.consume_root(current);drain(a);cleanup(d,b);});
 test("real-dequeued-old-timer-cannot-defer-new-cycle",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(3);d.consume_root(take(d.window(),poke_message));const auto old_id=d.timer_id();MSG stale{};
   nested_until([&]{return PeekMessageW(&stale,d.window(),WM_TIMER,WM_TIMER,PM_REMOVE)!=FALSE;});require(stale.wParam==old_id,"actual old timer");while(d.service_turn()){}m.add(3);d.consume_root(take(d.window(),poke_message));require(d.timer_id()!=old_id,"timer ID reused");
   const auto before=m.notifies.load();DispatchMessageW(&stale);require(m.notifies==before,"old timer granted new fallback");while(d.service_turn()){}cleanup(d,m);});
 test("timer-failure-restores-original-notification",[]{hooks api;api.timer=failed_timer;dispatcher d(api);host h;d.begin_root();model m(d,h);m.add(2);d.consume_root(take(d.window(),poke_message));
   require(d.failed()&&d.timer_id()==0&&m.notifies>0,"timer failure silently owns wake");cleanup(d,m);});
 test("nonzero-settimer-return-is-not-requested-id",[]{hooks api;api.timer=different_timer_return;dispatcher d(api);host h;d.begin_root();model m(d,h);m.add(3);d.consume_root(take(d.window(),poke_message));
   require(!d.failed()&&d.timer_id()!=0,"legal nonzero timer result rejected");MSG timer{};nested_until([&]{return PeekMessageW(&timer,d.window(),WM_TIMER,WM_TIMER,PM_REMOVE)!=FALSE;});require(timer.wParam==d.timer_id(),"requested HWND timer ID not retained");d.consume_root(timer);while(d.service_turn()){}cleanup(d,m);});
 test("kill-failure-keeps-retired-identity-forbids-rearm-until-destroy",[]{hooks api;api.kill=failed_cancel;dispatcher d(api);host h;d.begin_root();model old(d,h);old.add(2);d.consume_root(take(d.window(),poke_message));const auto id=d.timer_id();const auto window=d.window();
   d.service_turn();require(d.failed()&&d.timer_cancel_failed()&&d.timer_id()==0&&d.retired_timer_id()==id&&d.timer_cleanup_owed(),"failed KillTimer was treated as stopped");d.detach(old.t);model next(d,h);next.add(1);const auto notified=next.notifies.load();
   auto actual=wait_timer(window);require(actual.wParam==id,"failure seam unexpectedly cancelled actual timer");DispatchMessageW(&actual);require(next.notifies==notified&&d.timer_id()==0&&d.retired_timer_id()==id,"old live timer authorized new registration/rearm");
   d.close();require(!IsWindow(window)&&!d.timer_cleanup_owed()&&d.timer_cancel_failed(),"DestroyWindow cleanup or sticky failure lost");drain(next);drain(old);});
 test("reentrant-service-cannot-claim-active-pump",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.after_read=[&]{m.after_read={};require(!d.service_turn(),"recursive pump entered");};m.add(2);d.consume_root(take(d.window(),poke_message));require(m.calls==1&&m.notifies>0,"reentry fallback missing");cleanup(d,m);});
 test("exception-restores-legacy-and-releases-service",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.throw_once=true;m.add(2);bool caught=false;try{d.consume_root(take(d.window(),poke_message));}catch(const std::runtime_error&){caught=true;}
   require(caught&&m.notifies>0,"exception did not delegate");cleanup(d,m);});
 test("detach-during-callback-worker-cannot-post-old-token",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.after_read=[&]{m.after_read={};d.detach(m.t);};m.add(1);d.consume_root(take(d.window(),poke_message));
   std::thread worker([&]{m.add(1);});worker.join();MSG x{};require(!PeekMessageW(&x,d.window(),poke_message,poke_message,PM_REMOVE),"worker posted retired dispatcher token");drain(m);});
 test("round-robin-one-adapter-per-turn-and-control-independent",[]{dispatcher d;host h;d.begin_root();model a(d,h),b(d,h);a.add(2);b.add(2);d.service_turn();require(a.calls+b.calls==1,"multiple adapter budget");d.service_turn();require(a.calls==1&&b.calls==1,"round robin");
   require(PostMessageW(h.window,control_message,0,0)!=FALSE,"control post");auto control=take(h.window,control_message);DispatchMessageW(&control);require(h.controls==1,"control notification suppressed");while(d.service_turn()){}cleanup(d,a);cleanup(d,b);});
 test("navigation-refill-and-loop-exit-keep-legacy-progress",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.after_read=[&]{m.after_read={};m.work=0;m.add(1);};m.add(1);d.consume_root(take(d.window(),poke_message));require(m.t.pending(),"navigation refill lost");d.end_root();require(m.notifies>0,"Run exit lost wake");cleanup(d,m);});
 test("close-during-callback-retains-inflight-state",[]{auto d=std::make_unique<dispatcher>();host h;d->begin_root();model m(*d,h);m.after_read=[&]{m.after_read={};d.reset();};m.add(1);auto msg=take(d->window(),poke_message);d->consume_root(msg);
   require(!d&&!m.t.attached()&&m.calls==1,"close retained stale service ownership");m.add(1);drain(m);});
 test("duplicate-root-rejected-with-outer-root-still-active",[]{dispatcher d;host h;d.begin_root();bool rejected=false;try{d.begin_root();}catch(const std::logic_error&){rejected=true;}require(rejected,"nested root was silently accepted");
   model m(d,h);m.add(1);d.consume_root(take(d.window(),poke_message));require(m.calls==1&&!m.t.pending(),"outer root changed by rejected begin");d.end_root();m.add(1);cleanup(d,m);});
 test("ack-only-consumes-real-token-without-servicing-or-clearing-pending",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(2);
   require(d.consume_root(take(d.window(),poke_message),false)&&m.calls==0&&m.t.pending(),"ack-only changed pending/work");
   m.add(1);MSG extra{};require(!PeekMessageW(&extra,d.window(),poke_message,poke_message,PM_REMOVE),"pending request restarted poke chain");
   while(d.service_turn()){}require(m.calls==3&&!m.t.pending(),"ack-only lost subsequent work");cleanup(d,m);});
 test("post-failure-stays-legacy-after-resume-and-new-root",[]{hooks api;api.post=failed_post;dispatcher d(api);host h;d.begin_root();model m(d,h);m.add(2);
   require(d.failed()&&m.notifies>0,"post failure hidden");d.resume_root();require(!d.service_turn()&&m.calls==0,"post failure resumed");
   d.end_root();d.begin_root();d.resume_root();require(!d.service_turn()&&m.calls==0,"new root cleared permanent failure");cleanup(d,m);});
 test("timer-failure-cannot-be-rearmed-by-root-resume",[]{hooks api;api.timer=failed_timer;dispatcher d(api);host h;d.begin_root();model m(d,h);m.add(2);
   d.consume_root(take(d.window(),poke_message),false);require(d.failed()&&m.calls==0&&m.notifies>0,"timer failure not delegated");
   d.resume_root();require(!d.service_turn()&&!d.timer_id()&&m.t.pending(),"timer failure resumed");cleanup(d,m);});
 test("temporary-root-exit-recovers-pending-without-emptying-it",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(2);d.consume_root(take(d.window(),poke_message),false);
   d.end_root();require(m.t.pending()&&m.calls==0,"exit lost pending");d.begin_root();d.resume_root();
   require(d.service_turn()&&m.calls==1&&m.t.pending(),"temporary legacy trap");while(d.service_turn()){}cleanup(d,m);});
 test("modal-resume-cannot-claim-still-serving-entry",[]{dispatcher d;host h;d.begin_root();model m(d,h);m.add(2);
   m.after_read=[&]{m.after_read={};d.before_external_callback();auto msg=take(d.window(),poke_message);DispatchMessageW(&msg);
     d.resume_root();require(!d.service_turn()&&m.calls==1,"resume reentered active pump");};
   d.service_turn();d.resume_root();require(d.service_turn()&&m.calls==2,"outer return failed to resume");cleanup(d,m);});
 std::printf("SDK continuation policy: %u cases passed; hidden Win32 behavior only, no performance qualification\n",cases);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
