#include "paint_probe.h"
#include "native_burst_marker.h"
#include <windowsx.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace owner_fairness {
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
  nonce_=static_cast<std::uint32_t>(std::stoul(run.substr(24),nullptr,16));
  require(nonce_!=0,"fresh nonzero marker nonce");
  const auto stem=std::string("Local\\SOUI-R41-")+std::to_string(GetCurrentProcessId())+"-"+birth+"-"+run;
  auto event=[&](const char* suffix){const auto name=stem+suffix;const std::wstring w(name.begin(),name.end());
    SetLastError(ERROR_SUCCESS);auto h=CreateEventW(nullptr,TRUE,FALSE,w.c_str());
    require(h&&GetLastError()!=ERROR_ALREADY_EXISTS,"fresh R41 event required");return h;};
  ready_event_=event("-Ready");start_event_=event("-LoadActive");finish_event_=event("-Finish");
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
  if(signaled&&!inputs_->closed)inputs_->close(q); // A false observation never closes/resets.
  ++finish_fence_calls_;finish_fence_qpc_=q;finish_fence_finished_=signaled&&inputs_->closed;
  return {{"finished",finish_fence_finished_},{"cohortEndQpc",std::to_string(inputs_->end)},
    {"observedQpc",std::to_string(q)}};
}
void paint_probe::poll_finish()noexcept{
  try{std::lock_guard<std::mutex> lock(mutex_);if(inputs_->armed&&!inputs_->closed&&input_finished())inputs_->close(now());}
  catch(...){callback_fault_.store(true);}
}
void paint_probe::dispatch(const MSG& message,std::uint64_t q)noexcept{dispatch_=message;retrieved_=q;dispatch_claimed_=false;}
void paint_probe::dispatch_done()noexcept{retrieved_=0;dispatch_claimed_=false;}
void paint_probe::mouse(HWND w,WPARAM wp,LPARAM lp)noexcept{
  try{std::lock_guard<std::mutex> lock(mutex_);input_row r{};r.receipt=now();r.extra=static_cast<std::uint64_t>(GetMessageExtraInfo());
    r.tag=(r.extra&0xfff00000ULL)==tag_magic?static_cast<unsigned>(r.extra&0xfffffULL):0;
    r.x=GET_X_LPARAM(lp);r.y=GET_Y_LPARAM(lp);r.thread=GetCurrentThreadId();r.stage=value_.stage;
    r.dispatch_tuple=!dispatch_claimed_&&retrieved_&&dispatch_.hwnd==w&&dispatch_.message==WM_MOUSEMOVE&&dispatch_.wParam==wp&&dispatch_.lParam==lp&&r.thread==owner_thread_;
    r.retrieved=r.dispatch_tuple?retrieved_:0;r.message_time=dispatch_.time;dispatch_claimed_=true;
    inputs_->record(r);
  }catch(...){callback_fault_.store(true);}
}
bool paint_probe::input_complete(){std::lock_guard<std::mutex> lock(mutex_);return inputs_->complete();}
bool paint_probe::input_failed(){std::lock_guard<std::mutex> lock(mutex_);return inputs_->closed&&inputs_->failed;}
nlohmann::json paint_probe::input_json(){std::lock_guard<std::mutex> lock(mutex_);nlohmann::json a=nlohmann::json::array();
  for(unsigned i=0;i<inputs_->stored;++i){const auto&r=inputs_->rows[i];a.push_back({r.ordinal,std::to_string(r.receipt),std::to_string(r.retrieved),std::to_string(r.extra),r.tag,r.x,r.y,r.thread,r.stage,r.message_time,r.dispatch_tuple,r.in_cohort});}
  return {{"schema","soui.r41-native-strip-ledger/v1"},{"capacity",native_capacity},{"seen",inputs_->seen},{"stored",inputs_->stored},
    {"tagged",inputs_->tagged},{"overflow",inputs_->overflow},{"failed",inputs_->failed},{"firstFault",inputs_->first_fault},
    {"beginQpc",std::to_string(inputs_->begin)},{"endQpc",std::to_string(inputs_->end)},{"maxNoDispatchGapQpc",std::to_string(inputs_->max_gap)},
    {"complete",inputs_->complete()},{"finishFenceCalls",finish_fence_calls_},{"finishFenceQpc",std::to_string(finish_fence_qpc_)},
    {"finishFenceFinished",finish_fence_finished_},{"columns",{"ordinal","receiptQpc","retrievedQpc","extraInfo","tag","x","y","thread","stage","messageTime","dispatchTuple","inCohort"}},{"rows",a}};
}
probe_snapshot paint_probe::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_); auto result=value_;
  result.fault=result.fault || callback_fault_.load(); return result;
}
LRESULT CALLBACK paint_probe::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
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
    if(self&&message==WM_MOUSEMOVE){self->mouse(window,wparam,lparam);return 0;}
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
  require(CloseHandle(ready_event_)&&CloseHandle(start_event_)&&CloseHandle(finish_event_),"R41 event close");
}
} // namespace owner_fairness
