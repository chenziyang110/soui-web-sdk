#pragma once
#include <windows.h>
#include <soui/diagnostics/input_frame_trace.h>
#include <cstdint>

namespace owner_fairness {
// Passive GetMessage observation only. It neither retrieves/dispatches another
// message nor creates a paint_probe guard or advances its application ordinal.
class message_retrieval_snapshot final {
 public:
  struct value { MSG message{};std::uint64_t qpc{},extra{}; };
  void retrieved(const MSG& message,HWND strip) noexcept {
    const bool input=message.message==WM_MOUSEMOVE&&message.hwnd==strip;
    const auto extra=input?static_cast<std::uint64_t>(static_cast<ULONG_PTR>(GetMessageExtraInfo())):0;
    const auto qpc=soui::sdk::diagnostics::input_frame_trace::now_qpc();
    // A private SDK message may legitimately have no application dispatch.
    // An input observation cannot be silently replaced in that way.
    if(pending_&&input_)failed_=true;
    last_={message,qpc,extra};pending_=true;input_=input;
    if(!qpc)failed_=true;
  }
  value for_dispatch(const MSG& actual) noexcept {
    const bool matches=pending_&&same(last_.message,actual);
    pending_=false;input_=false;
    if(matches)return last_;
    failed_=true;
    // Unknown retrieval time stays unknown. The existing native tuple policy
    // rejects a zero retrieval stamp; do not manufacture a later timestamp.
    return {actual,0,0};
  }
  bool failed()const noexcept{return failed_||(pending_&&input_);}
  bool input_pending()const noexcept{return pending_&&input_;}
 private:
  static bool same(const MSG&a,const MSG&b)noexcept{
    return a.hwnd==b.hwnd&&a.message==b.message&&a.wParam==b.wParam&&a.lParam==b.lParam&&
      a.time==b.time&&a.pt.x==b.pt.x&&a.pt.y==b.pt.y;
  }
  value last_{};
  bool pending_{},input_{},failed_{};
};
}
