#pragma once

struct native_controls_resources {
  HMODULE module = nullptr;
  ~native_controls_resources() { if (module) FreeLibrary(module); }
};

// Demo-owned native island: a sibling HWND above the renderer child. These
// controls are SOUI objects, not DOM elements or system BUTTON/EDIT windows.
class native_controls_demo final : public SOUI::SHostWnd {
 public:
  bool open(HWND owner) {
    if (!Create(owner, WS_CHILD | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT,
                28, 150, 370, 350)) return false;
    pugi::xml_document document;
    if (!document.load(LR"xml(
<SOUI wndType="normal" width="370" height="350">
 <root colorBkgnd="#f0f6ff" font="face:Microsoft YaHei,size:12">
  <text pos="20,18,-20,48" colorText="#174b85">Native SOUI controls</text>
  <text pos="20,53,-20,78">This panel is rendered by C++ / SOUI.</text>
  <edit name="native_text" pos="20,90,-20,125" cueText="Type here / 输入中文" ncSkin="_skin.sys.border"/>
  <check pos="20,140,-20,168">原生 SOUI 复选框</check>
  <sliderbar pos="20,185,-20,215"/>
  <button id="7101" pos="20,230,175,268">点击原生按钮</button>
  <text name="native_count" pos="20,285,-20,320">原生点击次数：0</text>
 </root>
</SOUI>)xml") || !InitFromXml(document.child(L"SOUI"))) {
      DestroyWindow();
      return false;
    }
    ::SetWindowPos(m_hWnd, HWND_TOP, 28, 150, 370, 350,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
  }
  void OnClick() {
    const auto text = L"原生点击次数：" + std::to_wstring(++clicks_);
    if (auto* label = FindChildByName(L"native_count"))
      label->SetWindowText(text.c_str());
  }
  EVENT_MAP_BEGIN()
    EVENT_ID_COMMAND(7101, OnClick)
  EVENT_MAP_END()
 private:
  unsigned clicks_ = 0;
};
