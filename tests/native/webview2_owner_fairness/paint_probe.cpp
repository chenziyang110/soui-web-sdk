#include "paint_probe.h"
#include "native_burst_marker.h"
#include <windowsx.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace owner_fairness {
using json=nlohmann::json;
namespace {
constexpr wchar_t probe_class[] = L"SouiR41NativeInputStrip";
constexpr UINT_PTR timer_id = 71;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}
std::uint64_t paint_probe::now() {
  LARGE_INTEGER tick{};
  if (!QueryPerformanceCounter(&tick) || tick.QuadPart <= 0) {
    value_.fault = true; return 0;
  }
  return static_cast<std::uint64_t>(tick.QuadPart);
}
void paint_probe::increment(std::uint64_t& count) {
  if (count == (std::numeric_limits<std::uint64_t>::max)()) value_.fault = true;
  else ++count;
}
void paint_probe::create(HWND parent, HINSTANCE instance, const std::string& run, const std::string& birth) {
  LARGE_INTEGER frequency{};
  require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "probe QPC frequency");
  value_.frequency = static_cast<std::uint64_t>(frequency.QuadPart);
  instance_ = instance;
  owner_thread_=GetCurrentThreadId();
  source_probe_.initialize(); // Cold optional capability; never resolved on the hot path.
  nonce_=static_cast<std::uint32_t>(std::stoul(run.substr(24),nullptr,16));
  require(nonce_!=0,"fresh nonzero marker nonce");
  const auto stem=std::string("Local\\SOUI-R45-")+std::to_string(GetCurrentProcessId())+"-"+birth+"-"+run;
  auto event=[&](const char* suffix){const auto name=stem+suffix;const std::wstring w(name.begin(),name.end());
    SetLastError(ERROR_SUCCESS);auto h=CreateEventW(nullptr,TRUE,FALSE,w.c_str());
    require(h&&GetLastError()!=ERROR_ALREADY_EXISTS,"fresh R41 event required");return h;};
  ready_event_=event("-Ready");start_event_=event("-LoadActive");
  sender_done_event_=event("-SenderDone");finish_event_=event("-Finish");
  WNDCLASSW type{}; type.hInstance = instance; type.lpfnWndProc = procedure; type.lpszClassName = probe_class;
  require(RegisterClassW(&type) != 0, "probe RegisterClass");
  window_ = CreateWindowExW(0, probe_class, L"Native paint progress", WS_CHILD | WS_VISIBLE,
      0, 720, 1100, 72, parent, nullptr, instance, this);
  require(window_ != nullptr, "probe CreateWindow");
  require(SetTimer(window_, timer_id, 20, nullptr) != 0, "probe SetTimer");
}
void paint_probe::invalidate() noexcept {
  try {
    const bool ok = InvalidateRect(window_, nullptr, FALSE) != FALSE;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok) value_.fault = true;
    if (ok && value_.stage >= 1 && value_.stage <= 4)
      increment(value_.phases[value_.stage - 1].invalidations);
  } catch (...) { callback_fault_.store(true); }
}
std::uint64_t paint_probe::advance(unsigned next) {
  std::lock_guard<std::mutex> lock(mutex_);
  require(next == value_.stage + 1 && next <= 5, "native phase order differs");
  const auto tick = now();
  if(next==3&&inputs_->armed&&!inputs_->closed)inputs_->fault(10); // Late completion never repairs the load.
  if (value_.stage) {
    auto& previous = value_.phases[value_.stage - 1];
    if (tick < previous.begin || tick < previous.last_paint || tick < previous.last_timer) value_.fault = true;
    else {
      previous.end = tick;
      previous.max_paint_gap = (std::max)(previous.max_paint_gap, tick - previous.last_paint);
      previous.max_timer_gap = (std::max)(previous.max_timer_gap, tick - previous.last_timer);
      if(!previous.echoes.finish(tick)||!previous.pumps.finish(tick))value_.fault=true;
    }
  }
  value_.stage = next;
  if (next <= 4) {
    auto& phase = value_.phases[next - 1];
    phase.begin = phase.last_paint = phase.last_timer = tick;
    phase.echoes.start(tick);phase.pumps.start(tick);
  }
  stage_.store(next);
  return tick;
}
void paint_probe::note(bool paint) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (value_.stage < 1 || value_.stage > 4) return;
  auto& phase = value_.phases[value_.stage - 1];
  auto& last = paint ? phase.last_paint : phase.last_timer;
  auto& gap = paint ? phase.max_paint_gap : phase.max_timer_gap;
  const auto tick = now();
  if (tick < last) value_.fault = true;
  else { gap = (std::max)(gap, tick - last); last = tick; }
  increment(paint ? phase.paints : phase.timers);
}
void paint_probe::owner_event() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (value_.stage >= 1 && value_.stage <= 4) increment(value_.phases[value_.stage - 1].owner_events);
}
paint_probe::pump_stamp paint_probe::pump_begin(){
  std::lock_guard<std::mutex> lock(mutex_);const auto q=now();
  if(value_.stage>=1&&value_.stage<=4&&!value_.phases[value_.stage-1].pumps.note(q))value_.fault=true;
  return {q,value_.stage};
}
void paint_probe::pump_end(pump_stamp begin){
  std::lock_guard<std::mutex> lock(mutex_);const auto q=now();
  if(q<begin.qpc)value_.fault=true;
  else if(begin.stage>=1&&begin.stage<=4){
    auto& phase=value_.phases[begin.stage-1];
    if(value_.stage!=begin.stage)++phase.cross_phase_pump_returns;
    else phase.max_pump_duration=(std::max)(phase.max_pump_duration,q-begin.qpc);
  }
}
void paint_probe::echo(){std::lock_guard<std::mutex> lock(mutex_);
  if(value_.stage>=1&&value_.stage<=4&&!value_.phases[value_.stage-1].echoes.note(now()))value_.fault=true;}
bool paint_probe::input_ready()const noexcept{return WaitForSingleObject(ready_event_,0)==WAIT_OBJECT_0;}
bool paint_probe::input_finished()const noexcept{return WaitForSingleObject(finish_event_,0)==WAIT_OBJECT_0;}
void paint_probe::cancel_input(){require(ResetEvent(start_event_)!=FALSE,"cancel input attempt on Host close");}
std::uint64_t paint_probe::load_active(){
  std::lock_guard<std::mutex> lock(mutex_);require(value_.stage==2&&value_.phases[1].echoes.count>0,"no active real echo");
  const auto q=now();require(inputs_->arm(q),"input arm repeated/invalid");
  require(SetEvent(start_event_)!=FALSE,"input load event failed");return q;
}
nlohmann::json paint_probe::input_finish_fence(){
  std::lock_guard<std::mutex> lock(mutex_);
  require(inputs_->armed&&finish_fence_calls_==0,"input finish fence must be once after arm");
  const bool signaled=input_finished();const auto q=now();
  if(signaled)inputs_->observe_sender_terminal(q);
  // This worker-side fence only observes the owner seal. Sender terminal is
  // neither a receiver-drain barrier nor permission to close the cohort here.
  ++finish_fence_calls_;finish_fence_qpc_=q;
  finish_fence_finished_=signaled&&inputs_->closed&&inputs_->sender_done&&inputs_->positive_sequence_complete();
  return {{"finished",finish_fence_finished_},{"cohortEndQpc",std::to_string(inputs_->end)},
    {"observedQpc",std::to_string(q)},{"senderDoneObserved",inputs_->sender_done},
    {"receiverDrained",inputs_->closed},{"strictValid",inputs_->complete()}};
}
void paint_probe::poll_finish()noexcept{
  try{
    std::lock_guard<std::mutex> lock(mutex_);
    // Once latched, do not add per-pump Win32 polling. The existing monitor
    // posts one owner opportunity even if every tag arrived before SenderDone.
    if(inputs_->armed&&!inputs_->sender_done&&WaitForSingleObject(sender_done_event_,0)==WAIT_OBJECT_0){
      inputs_->observe_sender_done(now());
      if(inputs_->sender_done&&!drain_notification_attempted_){
        drain_notification_attempted_=true;
        drain_notification_posted_=PostMessageW(window_,drain_message,0,0)!=FALSE;
        if(!drain_notification_posted_){inputs_->fault(12);callback_fault_.store(true);}
      }
    }
    if(inputs_->armed&&!inputs_->sender_terminal&&input_finished())inputs_->observe_sender_terminal(now());
  }
  catch(...){callback_fault_.store(true);}
}
void paint_probe::dispatch(const MSG& message,std::uint64_t q,std::uint64_t extra)noexcept{
  ++dispatch_depth_;
  if(dispatch_depth_>dispatch_stack_.size()||retrieval_ordinal_==UINT64_MAX){
    dispatch_context_fault_.store(true);callback_fault_.store(true);return;
  }
  dispatch_stack_[dispatch_depth_-1]={message,q,extra,++retrieval_ordinal_,false};
}
void paint_probe::dispatch_done()noexcept{
  if(!dispatch_depth_){dispatch_context_fault_.store(true);callback_fault_.store(true);return;}
  if(dispatch_depth_<=dispatch_stack_.size())dispatch_stack_[dispatch_depth_-1]={};
  --dispatch_depth_;
}
void paint_probe::owner_safe_point(){
  require(GetCurrentThreadId()==owner_thread_,"receiver seal must run on owner");
  std::lock_guard<std::mutex> lock(mutex_);
  if(!drain_notification_received_qpc_)drain_notification_received_qpc_=now();
  if(inputs_->sender_done&&inputs_->positive_sequence_complete())seal_on_owner_locked();
}
void paint_probe::seal_on_owner_locked(){
  require(GetCurrentThreadId()==owner_thread_,"receiver seal must run on owner");
  if(inputs_->seal_on_owner(now())){
    receiver_drain_thread_=GetCurrentThreadId();
    receiver_drain_retrieval_ordinal_=retrieval_ordinal_;
  }
}
void paint_probe::mouse(HWND w,WPARAM wp,LPARAM lp,const entry_sample& entry)noexcept{
  try{std::lock_guard<std::mutex> lock(mutex_);input_row r{};r.receipt=entry.qpc;r.entry_extra=entry.extra;
    auto* frame=dispatch_depth_&&dispatch_depth_<=dispatch_stack_.size()?&dispatch_stack_[dispatch_depth_-1]:nullptr;
    const bool matched=frame&&!frame->claimed&&frame->retrieved&&frame->message.hwnd==w&&frame->message.message==WM_MOUSEMOVE&&
      frame->message.wParam==wp&&frame->message.lParam==lp&&GetCurrentThreadId()==owner_thread_;
    r.retrieval_known=matched;r.extra=matched?frame->extra:entry.extra;
    r.tag=(r.extra&0xfff00000ULL)==tag_magic?static_cast<unsigned>(r.extra&0xfffffULL):0;
    r.x=GET_X_LPARAM(lp);r.y=GET_Y_LPARAM(lp);r.thread=GetCurrentThreadId();r.stage=value_.stage;
    r.dispatch_depth=dispatch_depth_;r.sent_flags=entry.sent_flags;
    r.dispatch_tuple=matched&&frame->extra==entry.extra&&!dispatch_context_fault_.load();
    if(matched){
      r.retrieved=frame->retrieved;r.retrieval_extra=frame->extra;r.retrieval_ordinal=frame->ordinal;
      r.message_time=frame->message.time;r.message_hwnd=reinterpret_cast<std::uintptr_t>(frame->message.hwnd);
      r.message_id=frame->message.message;r.message_wparam=frame->message.wParam;
      r.message_lparam=static_cast<std::uintptr_t>(frame->message.lParam);
      r.message_pt_x=frame->message.pt.x;r.message_pt_y=frame->message.pt.y;frame->claimed=true;
    }
    r.source_attempted=entry.source.attempted;r.source_ok=entry.source.ok;r.source_error=entry.source.error;
    r.source_device=entry.source.value.device;r.source_origin=entry.source.value.origin;
    inputs_->record(r);
    // Real owner callback, after recording its actual identity. No nested pump,
    // Win32 wait, synthetic input, or second notification is introduced here.
    if(inputs_->sender_done&&inputs_->positive_sequence_complete())seal_on_owner_locked();
  }catch(...){callback_fault_.store(true);}
}
bool paint_probe::input_complete(){std::lock_guard<std::mutex> lock(mutex_);return inputs_->complete();}
bool paint_probe::input_failed(){std::lock_guard<std::mutex> lock(mutex_);return inputs_->closed&&inputs_->failed;}
nlohmann::json paint_probe::input_json(){std::lock_guard<std::mutex> lock(mutex_);nlohmann::json a=nlohmann::json::array();
  for(unsigned i=0;i<inputs_->stored;++i){const auto&r=inputs_->rows[i];a.push_back({r.ordinal,r.receipt?json(std::to_string(r.receipt)):json(nullptr),
    r.retrieval_known?json(std::to_string(r.retrieved)):json(nullptr),std::to_string(r.extra),r.tag,r.x,r.y,r.thread,r.stage,
    r.retrieval_known?json(r.message_time):json(nullptr),r.dispatch_tuple,r.in_cohort,
    r.retrieval_known?json(std::to_string(r.retrieval_ordinal)):json(nullptr),r.retrieval_known?json(std::to_string(r.retrieval_extra)):json(nullptr),
    std::to_string(r.entry_extra),r.retrieval_known?json(std::to_string(r.message_hwnd)):json(nullptr),
    r.retrieval_known?json(r.message_id):json(nullptr),r.retrieval_known?json(std::to_string(r.message_wparam)):json(nullptr),
    r.retrieval_known?json(std::to_string(r.message_lparam)):json(nullptr),r.retrieval_known?json(r.message_pt_x):json(nullptr),
    r.retrieval_known?json(r.message_pt_y):json(nullptr),r.dispatch_depth,r.sent_flags,r.source_attempted,
    r.source_attempted?json(r.source_ok):json(nullptr),r.source_attempted&&!r.source_ok?json(r.source_error):json(nullptr),
    r.source_ok?json(r.source_device):json(nullptr),r.source_ok?json(r.source_origin):json(nullptr),r.identity_window});}
  return {{"schema","soui.r45-native-strip-ledger/v2"},{"capacity",native_capacity},{"seen",inputs_->seen},{"stored",inputs_->stored},
    {"tagged",inputs_->tagged},{"overflow",inputs_->overflow},{"failed",inputs_->failed},{"firstFault",inputs_->first_fault},
    {"positiveRows",inputs_->positive_rows},{"positiveSequenceComplete",inputs_->positive_sequence_complete()},
    {"senderDoneObserved",inputs_->sender_done},{"senderDoneObservedQpc",std::to_string(inputs_->sender_done_qpc)},
    {"senderTerminalObserved",inputs_->sender_terminal},{"senderTerminalObservedQpc",inputs_->sender_terminal?json(std::to_string(inputs_->sender_terminal_qpc)):json(nullptr)},
    {"receiverDrained",inputs_->closed},{"receiverDrainOwnerThread",receiver_drain_thread_?json(receiver_drain_thread_):json(nullptr)},
    {"receiverDrainRetrievalOrdinal",receiver_drain_thread_?json(std::to_string(receiver_drain_retrieval_ordinal_)):json(nullptr)},
    {"drainNotificationAttempted",drain_notification_attempted_},{"drainNotificationPosted",drain_notification_posted_},
    {"drainNotificationReceivedQpc",drain_notification_received_qpc_?json(std::to_string(drain_notification_received_qpc_)):json(nullptr)},
    {"dispatchContextFault",dispatch_context_fault_.load()},
    {"inputMessageSource",{{"available",source_probe_.available()},{"resolveError",source_probe_.resolve_error()},
      {"modulePath",source_probe_.module_path().empty()?json(nullptr):json(std::filesystem::path(source_probe_.module_path()).generic_string())},{"diagnosticOnly",true},{"sourceIdentityAuthenticated",false}}},
    {"beginQpc",std::to_string(inputs_->begin)},{"endQpc",std::to_string(inputs_->end)},{"maxNoDispatchGapQpc",std::to_string(inputs_->max_gap)},
    {"complete",inputs_->complete()},{"finishFenceCalls",finish_fence_calls_},{"finishFenceQpc",std::to_string(finish_fence_qpc_)},
    {"finishFenceFinished",finish_fence_finished_},{"columns",{"ordinal","receiptQpc","retrievedQpc","extraInfo","tag","x","y","thread","stage","messageTime","dispatchTuple","inCohort",
      "retrievalOrdinal","retrievalExtraInfo","wndProcEntryExtraInfo","messageHwnd","messageId","messageWParam","messageLParam","messagePtX","messagePtY","dispatchDepth","inSendMessageFlags",
      "sourceAttempted","sourceSuccess","sourceError","sourceDeviceType","sourceOriginId","strictIdentityWindow"}},{"rows",a}};
}
probe_snapshot paint_probe::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_); auto result=value_;
  result.fault=result.fault || callback_fault_.load(); return result;
}
LRESULT CALLBACK paint_probe::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  entry_sample entry{};
  if(message==WM_MOUSEMOVE){
    // First read at entry: later metadata APIs must not supply its identity.
    entry.extra=static_cast<std::uint64_t>(static_cast<ULONG_PTR>(GetMessageExtraInfo()));
    LARGE_INTEGER q{};if(QueryPerformanceCounter(&q)&&q.QuadPart>0)entry.qpc=q.QuadPart;
    entry.sent_flags=InSendMessageEx(nullptr);
  }
  auto* self = reinterpret_cast<paint_probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<paint_probe*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  try {
    if (self && message == WM_PAINT) {
      self->note(true); // Dispatch observation, not a unique frame or completed present.
      PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
      std::uint32_t nonce{},count{},frame{};
      {std::lock_guard<std::mutex> lock(self->mutex_);nonce=self->inputs_->failed?0:self->nonce_;
        count=self->inputs_->tagged;frame=self->marker_frame_++;}
      const bool good = dc && FillRect(dc, &paint.rcPaint, reinterpret_cast<HBRUSH>(COLOR_HIGHLIGHT + 1))&&
        soui::benchmark::native_burst_marker::draw(dc,nonce,count,frame,count);
      const bool ended = EndPaint(window, &paint) != FALSE;
      if (!good || !ended) self->callback_fault_.store(true);
      return 0;
    }
    if(self&&message==WM_MOUSEMOVE){entry.source=self->source_probe_.sample();self->mouse(window,wparam,lparam,entry);return 0;}
    if(self&&message==drain_message){self->owner_safe_point();return 0;}
    if (self && message == WM_TIMER && wparam == timer_id) { self->note(false); return 0; }
  } catch (...) {
    if (self) self->callback_fault_.store(true);
    // The normal default procedure validates paint after an observer failure.
    // No C++ exception crosses the Win32 callback and no failed sample qualifies.
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
void paint_probe::destroy() {
  require(KillTimer(window_, timer_id) != FALSE, "probe KillTimer");
  require(DestroyWindow(window_) != FALSE, "probe DestroyWindow"); window_ = nullptr;
  require(UnregisterClassW(probe_class, instance_) != FALSE, "probe UnregisterClass");
  require(CloseHandle(ready_event_)&&CloseHandle(start_event_)&&CloseHandle(sender_done_event_)&&CloseHandle(finish_event_),"R45 event close");
}
} // namespace owner_fairness
