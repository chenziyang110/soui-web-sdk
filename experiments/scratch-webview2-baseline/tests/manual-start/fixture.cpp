#include "../../manual-profile-start.h"
#include <iostream>

namespace {
using soui::scratch_diagnostic::manual_profile_start;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void policy() {
  manual_profile_start automatic;
  require(!automatic.waiting() && !automatic.request(true,true) && automatic.page_admitted(false,false),
      "Automatic mode changed its existing admission behavior");
  manual_profile_start manual(true);
  require(manual.waiting() && !manual.page_admitted(true,true),"Manual mode admitted before an explicit start");
  require(!manual.request(false,true) && !manual.request(true,false) && !manual.requested(),
      "Readiness/foreground request guards were bypassed");
  require(manual.request(true,true) && !manual.waiting() && !manual.request(true,true),
      "Manual start was not one-shot");
  require(!manual.page_admitted(false,true) && !manual.page_admitted(true,false) && manual.page_admitted(true,true),
      "Manual capture bypassed native foreground or actual page focus");
  const auto anchor=soui::scratch_diagnostic::profile_budget_begin(2000,1000);
  require(anchor==1000 && soui::scratch_diagnostic::profile_budget_begin(9000,anchor)==1000 &&
      soui::scratch_diagnostic::profile_budget_begin(2000,0)==2000,"Manual waiting reset the original budget");
  bool rejected=false;try{(void)soui::scratch_diagnostic::profile_budget_begin(2000,2001);}catch(const std::runtime_error&){rejected=true;}
  require(rejected,"A future deadline anchor was accepted");
}
void hidden_menu() {
  const auto instance=GetModuleHandleW(nullptr);
  WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=DefWindowProcW;type.lpszClassName=L"SouiManualProfileStartFixture";
  require(RegisterClassW(&type)!=0,"Hidden fixture registration failed");
  struct cleanup {HINSTANCE instance;HWND window{};~cleanup(){if(window)DestroyWindow(window);UnregisterClassW(L"SouiManualProfileStartFixture",instance);}} guard{instance};
  RECT frame{0,0,1424,861};require(AdjustWindowRectEx(&frame,WS_OVERLAPPEDWINDOW,TRUE,0)!=FALSE,"Menu frame sizing failed");
  guard.window=CreateWindowExW(0,type.lpszClassName,L"Hidden gate fixture",WS_OVERLAPPEDWINDOW,0,0,
      frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,instance,nullptr);
  require(guard.window!=nullptr && !IsWindowVisible(guard.window),"Hidden fixture became visible");
  manual_profile_start gate(true);gate.attach(guard.window);
  const auto menu=GetMenu(guard.window);require(menu && IsMenu(menu),"Native start menu was not attached");
  RECT client{};require(GetClientRect(guard.window,&client) && client.right==1424 && client.bottom==861,
      "Native menu changed the frozen content viewport");
  require((GetMenuState(menu,gate.command_id,MF_BYCOMMAND)&MF_GRAYED)!=0,"Unready menu was enabled");
  gate.reflect_readiness(guard.window,true);
  require((GetMenuState(menu,gate.command_id,MF_BYCOMMAND)&MF_GRAYED)==0,"Ready menu did not enable");
  require(!soui::scratch_diagnostic::profile_owner_guard(guard.window),"A hidden owner passed foreground admission");
  // Synthetic policy conditions only: no UI message/input is injected.
  require(gate.request(true,true),"One-shot policy request failed");gate.reflect_readiness(guard.window,true);
  require((GetMenuState(menu,gate.command_id,MF_BYCOMMAND)&MF_GRAYED)!=0,"Started menu stayed enabled");
  require(DestroyWindow(guard.window)!=FALSE,"Hidden fixture did not close");guard.window=nullptr;
  require(!IsMenu(menu),"Window-owned menu was not retired");
}
}
int main(){try{policy();hidden_menu();std::cout<<"Manual gate policy and hidden native-menu fixture passed; no input or visible-window qualification.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
