#pragma once
#include <set>
#include <stdexcept>
#include <cstring>

// The methods below are exact production byte projections. Only owner/view,
// paint lifetime and commit publication are explicit fakes; no real renderer.
namespace provider_fixture {
inline bool owner = true, paint_available = true, commit_throws = false;
inline int reentry = 0;
inline void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
}
namespace content {
inline bool isMainThread() { return provider_fixture::owner; }
enum WebPageState { pageUninited, pageInited, pageDestroying, pageDestroyed };
class CheckReEnter {
 public:
  static int getEnterCount() { return provider_fixture::reentry; }
};
class PaintActivityScope {
 public:
  explicit PaintActivityScope(bool available) : available_(available) {}
  explicit operator bool() const { return available_; }
 private:
  bool available_;
};
class LayerTreeHost {
 public:
  void setDrawMinInterval(double interval) { last_interval = interval; ++updates; }
  double last_interval = 0;
  unsigned updates = 0;
};
class WebPageImpl {
 public:
  void setMainFrameRate(int rate);
  bool applyMainFrameRate(int rate);
  int mainFrameRate() const;
  void setNeedsCommit() {
    ++commits;
    if (provider_fixture::commit_throws) throw std::runtime_error("explicit commit allocation fake failure");
  }
  WebPageState m_state = pageInited;
  double m_mainFrameInterval = 0, m_nextMainFrameTime = 123;
  int m_appliedMainFrameRate = 0;
  LayerTreeHost layer;
  LayerTreeHost* m_layerTreeHost = &layer;
  unsigned commits = 0;
};
#define ASSERT(value) provider_fixture::require(!!(value), "native owner assert")
#include "frame-policy-methods.inc"
#undef ASSERT
class WebPage {
 public:
  bool applyMainFrameRate(int rate);
  int mainFrameRate() const;
  bool paintLifetimeState() const { return provider_fixture::paint_available; }
  WebPageImpl impl;
  WebPageImpl* m_pageImpl = &impl;
};
#include "frame-policy-wrapper.inc"
}
using wkeWebView = void*;
namespace wke {
inline std::set<wkeWebView> g_liveWebViews;
inline bool inputUiOwnerAvailable() { return provider_fixture::owner; }
class CWebView {
 public:
  CWebView() { g_liveWebViews.insert(this); }
  ~CWebView() { g_liveWebViews.erase(this); }
  content::WebPage* webPage() { return page_available ? &page : nullptr; }
  content::WebPage page;
  bool page_available = true;
};
}
namespace provider_fixture {
#include "frame-policy-provider.inc"
inline wkeInputUiFramePolicy output() {
  return {sizeof(wkeInputUiFramePolicy), WKE_INPUT_UI_FRAME_POLICY_ABI_VERSION, 0, 0, 0, 0, {0, 0}};
}
inline void run() {
  wke::CWebView view;
  auto result = output();
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_OK
      && result.minRate == 30 && result.maxRate == 240 && result.appliedRate == 0, "actual initial disabled query");
  require(applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_OK && result.appliedRate == 144,
      "actual checked setter and query agree");
  require(view.page.impl.layer.updates == 1 && view.page.impl.commits == 1, "valid change updates draw gate once");
  require(applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_OK
      && view.page.impl.layer.updates == 1 && view.page.impl.commits == 1, "same setter is idempotent");
  const auto before = result;
  for (unsigned invalid : {1U, 29U, 241U, 360U, 1000U, 0xffffffffU}) {
    require(applyFramePolicy(&view, invalid, &result) == WKE_INPUT_UI_INVALID
        && std::memcmp(&result, &before, sizeof(result)) == 0
        && view.page.mainFrameRate() == 144, "invalid rate has no mutation or success output");
  }
  require(applyFramePolicy(&view, 0, &result) == WKE_INPUT_UI_OK && !result.appliedRate
      && view.page.impl.m_mainFrameInterval == 0, "zero disables main controller");
  require(view.page.impl.layer.updates == 1, "zero preserves legacy draw gate for host to restore");
  view.page.impl.setMainFrameRate(120);
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_OK && result.appliedRate == 120,
      "legacy debug setter reflected in actual query");
  view.page.impl.setMainFrameRate(360);
  require(view.page.mainFrameRate() == 120, "legacy invalid setter remains ignored");
  auto invalid_output = result; invalid_output.structSize -= 4;
  require(queryFramePolicy(&view, &invalid_output) == WKE_INPUT_UI_INVALID, "short output rejected");
  invalid_output = result; invalid_output.abiVersion = 2;
  require(applyFramePolicy(&view, 144, &invalid_output) == WKE_INPUT_UI_INVALID, "wrong snapshot ABI rejected");
  invalid_output = result; invalid_output.reserved[0] = 1;
  require(queryFramePolicy(&view, &invalid_output) == WKE_INPUT_UI_INVALID, "unknown reserved bits rejected");
  require(queryFramePolicy(&view, nullptr) == WKE_INPUT_UI_INVALID, "null snapshot rejected");
  require(queryFramePolicy(nullptr, &result) == WKE_INPUT_UI_UNAVAILABLE, "null view rejected");
  int stale_view = 0;
  require(applyFramePolicy(&stale_view, 144, &result) == WKE_INPUT_UI_UNAVAILABLE, "unregistered view rejected");
  owner = false;
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_UNAVAILABLE
      && !view.page.applyMainFrameRate(144) && view.page.mainFrameRate() == -1, "nonowner is never accepted");
  owner = true; paint_available = false;
  require(applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_REJECTED, "closing paint lifetime rejects apply");
  paint_available = true; reentry = 1;
  require(applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_BUSY
      && view.page.mainFrameRate() == 120, "reentrant apply is transient without mutation");
  reentry = 0; view.page.impl.m_state = content::pageDestroying;
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_UNAVAILABLE
      && applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_REJECTED, "destroying state cannot apply");
  view.page.impl.m_state = content::pageInited;
  const auto pre_failure = result;
  commit_throws = true;
  require(applyFramePolicy(&view, 144, &result) == WKE_INPUT_UI_REJECTED
      && std::memcmp(&result, &pre_failure, sizeof(result)) == 0, "partial scheduling failure is not success");
  commit_throws = false;
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_OK && result.appliedRate == 144,
      "query reconciles actual policy after scheduling failure");
  view.page.m_pageImpl = nullptr;
  require(!view.page.applyMainFrameRate(144) && view.page.mainFrameRate() == -1, "retired impl is unavailable");
  view.page.m_pageImpl = &view.page.impl; view.page_available = false;
  require(queryFramePolicy(&view, &result) == WKE_INPUT_UI_UNAVAILABLE, "retired page is unavailable");
}
} // namespace provider_fixture
