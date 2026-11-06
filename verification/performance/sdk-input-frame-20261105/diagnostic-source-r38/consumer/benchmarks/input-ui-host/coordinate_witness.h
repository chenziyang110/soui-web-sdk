#pragma once
// External test consumer only. This never changes SDK input coordinates.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <stdexcept>
#include <json.hpp>

class coordinate_witness final {
 public:
  coordinate_witness() : owner_thread_(GetCurrentThreadId()) {}
  coordinate_witness(const coordinate_witness&)=delete;
  ~coordinate_witness() { detach(); }
  void attach(HWND owner) {
    if(child_||GetCurrentThreadId()!=owner_thread_)throw std::runtime_error("Witness owner/attachment invalid");
    struct search { HWND parent,found{}; DWORD thread,pid; unsigned count{}; } s{owner,nullptr,owner_thread_,GetCurrentProcessId(),0};
    EnumChildWindows(owner,[](HWND child,LPARAM raw)->BOOL {
      auto& state=*reinterpret_cast<search*>(raw);wchar_t name[80]{};DWORD pid{};
      const auto thread=GetWindowThreadProcessId(child,&pid);
      if(GetClassNameW(child,name,80)&&std::wstring(name)==L"SouiMiniblinkInteractionWindowV1"&&
          pid==state.pid&&thread==state.thread&&GetParent(child)==state.parent){state.found=child;++state.count;}
      return TRUE;
    },reinterpret_cast<LPARAM>(&s));
    RECT client{};
    if(s.count!=1||!GetClientRect(s.found,&client)||client.right!=1100||client.bottom!=720)
      throw std::runtime_error("Witness requires one same-thread SDK child with exact client geometry");
    child_=s.found;attached_child_=child_;
    if(!SetWindowSubclass(child_,procedure,subclass_id,reinterpret_cast<DWORD_PTR>(this))){child_=nullptr;throw std::runtime_error("Witness subclass installation failed");}
    attached_=true;
  }
  void detach() noexcept {
    if(child_&&GetCurrentThreadId()==owner_thread_){
      if(IsWindow(child_)&&!RemoveWindowSubclass(child_,procedure,subclass_id))detach_failed_=true;
      child_=nullptr;
    }
  }
  bool complete() const noexcept {
    return attached_&&destroyed_&&!child_&&!detach_failed_&&!overflow_&&!clock_failed_&&stored_>0&&tagged_seen_>0;
  }
  bool tagged_coordinate_mismatch() const noexcept {return tagged_coordinate_mismatch_;}
  std::string result(const std::string& run) const {
    nlohmann::json rows=nlohmann::json::array();
    for(std::size_t index=0;index<stored_;++index){const auto& r=rows_[index];
      rows.push_back({{"ordinal",index+1},{"qpc",std::to_string(r.qpc)},{"message",r.message},
        {"hwnd",std::to_string(r.hwnd)},{"lparam",std::to_string(r.lparam)},
        {"client_x",r.x},{"client_y",r.y},{"extra_info",std::to_string(r.extra)},
        {"decoded_tag",r.tag},{"cursor_read_ok",r.cursor_ok},
        {"cursor_screen_x",r.cursor.x},{"cursor_screen_y",r.cursor.y}});}
    return nlohmann::json{{"schema","soui.input-coordinate-native-witness/v1"},{"run_id",run},
      {"diagnostic_only",true},{"sdk_input_modified",false},{"engine_packet_observed",false},
      {"child_hwnd",std::to_string(reinterpret_cast<std::uintptr_t>(attached_child_))},
      {"attached",attached_},{"detached_on_nc_destroy",destroyed_},{"detach_failed",detach_failed_},
      {"capacity",rows_.size()},{"events_seen",seen_},{"events_stored",stored_},{"overflow",overflow_},
      {"clock_failed",clock_failed_},{"tagged_events_seen",tagged_seen_},{"capture_complete",complete()},
      {"tagged_coordinate_mismatch",tagged_coordinate_mismatch_},{"rows",rows},{"performanceGate",false},{"releaseReady",false}}.dump();
  }
 private:
  struct row { std::uint64_t qpc{},hwnd{},lparam{},extra{};UINT message{};int x{},y{};std::uint32_t tag{};POINT cursor{};bool cursor_ok{}; };
  void record(HWND window,UINT message,LPARAM lparam) noexcept {
    ++seen_;
    if(stored_==rows_.size()){overflow_=true;return;}
    auto& r=rows_[stored_++];LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now)||now.QuadPart<=0)clock_failed_=true;
    r.qpc=static_cast<std::uint64_t>(now.QuadPart);r.message=message;
    r.hwnd=reinterpret_cast<std::uintptr_t>(window);r.lparam=static_cast<std::uintptr_t>(lparam);
    r.x=GET_X_LPARAM(lparam);r.y=GET_Y_LPARAM(lparam);
    r.extra=static_cast<std::uintptr_t>(GetMessageExtraInfo());
    if((r.extra&0xfff00000ULL)==0x53400000ULL){
      r.tag=static_cast<std::uint32_t>(r.extra&0x000fffffULL);
      if(r.tag){++tagged_seen_;
        const auto ordinal=r.tag-1;
        if(r.tag>64||r.x!=100+static_cast<int>(ordinal%1000)||r.y!=100+static_cast<int>(ordinal/1000))
          tagged_coordinate_mismatch_=true;
      }
    }
    r.cursor_ok=GetCursorPos(&r.cursor)!=FALSE;
  }
  static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR raw) {
    auto* self=reinterpret_cast<coordinate_witness*>(raw);
    if(message==WM_MOUSEMOVE)self->record(window,message,lparam);
    if(message==WM_NCDESTROY){
      if(!RemoveWindowSubclass(window,procedure,subclass_id))self->detach_failed_=true;
      self->child_=nullptr;self->destroyed_=true;
    }
    return DefSubclassProc(window,message,wparam,lparam);
  }
  static constexpr UINT_PTR subclass_id=0x534f5549;
  DWORD owner_thread_;HWND child_{},attached_child_{};bool attached_{},destroyed_{},detach_failed_{},overflow_{},clock_failed_{};
  std::array<row,256> rows_{};std::size_t stored_{};std::uint64_t seen_{},tagged_seen_{};bool tagged_coordinate_mismatch_{};
};

class coordinate_page_report final {
 public:
  explicit coordinate_page_report(std::string run):run_(std::move(run)){}
  std::string submit(const std::string& params) {
    if(params.size()>32768)throw std::invalid_argument("Witness params exceed bound");
    const auto value=nlohmann::json::parse(params);
    if(!value.is_object()||value.size()!=1||!value.contains("snapshot_json")||!value["snapshot_json"].is_string())
      throw std::invalid_argument("Witness snapshot must be a bounded string");
    const auto bytes=value["snapshot_json"].get<std::string>();
    if(bytes.empty()||bytes.size()>24576)throw std::invalid_argument("Witness page snapshot exceeds bound");
    const auto snapshot=nlohmann::json::parse(bytes);
    if(!snapshot.is_object()||snapshot.value("schema",std::string{})!="soui.input-coordinate-page-witness/v1"||
       snapshot.value("run_id",std::string{})!=run_||!snapshot.contains("rows")||!snapshot["rows"].is_array()||snapshot["rows"].size()>128||
       !snapshot.contains("overflow")||!snapshot["overflow"].is_boolean()||
       !snapshot.contains("invalid")||!snapshot["invalid"].is_boolean()||
       !snapshot.contains("first_invalid")||!(snapshot["first_invalid"].is_null()||snapshot["first_invalid"].is_object()))
      throw std::invalid_argument("Witness page identity/row bound differs");
    for(const auto& r:snapshot["rows"]){
      if(!r.is_object()||!r.contains("x")||!r.contains("y")||!r["x"].is_number()||!r["y"].is_number()||
         !std::isfinite(r["x"].get<double>())||!std::isfinite(r["y"].get<double>()))
        throw std::invalid_argument("Witness page coordinate invalid");
    }
    std::lock_guard<std::mutex> guard(mutex_);
    if(!bytes_.empty())throw std::invalid_argument("Witness page snapshot already admitted");
    bytes_=bytes;
    admitted_tick_.store(GetTickCount64(),std::memory_order_release);
    return "{\"accepted\":true,\"persisted\":false}";
  }
  // Owner checks this publication only; it never waits for the Bridge mutex.
  std::uint64_t admitted_tick() const noexcept {return admitted_tick_.load(std::memory_order_acquire);}
  std::string result() {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto snapshot=bytes_.empty()?nlohmann::json(nullptr):nlohmann::json::parse(bytes_);
    return nlohmann::json{{"schema","soui.input-coordinate-page-receipt/v1"},{"run_id",run_},
      {"accepted",!bytes_.empty()},{"admitted_tick",std::to_string(admitted_tick())},
      {"capture_complete",!bytes_.empty()&&!snapshot["rows"].empty()&&!snapshot["overflow"].get<bool>()},
      {"snapshot",snapshot},
      {"performanceGate",false},{"releaseReady",false}}.dump();
  }
 private:
  std::string run_,bytes_;std::mutex mutex_;std::atomic<std::uint64_t> admitted_tick_{0};
};
