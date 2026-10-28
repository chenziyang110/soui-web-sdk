#pragma once

// Private composition seam for the Stage0 executable only. Never installed.
#ifndef SOUI_HOST_STAGE0_DEVTOOLS_TEST_ONLY
#error "Stage0 Host borrowing is available only to the dedicated experiment"
#endif

#include <soui/host/engine_thread.h>
#include "../../../third_party/include/miniblink/wke.h"

namespace soui::sdk::detail {

// A point-in-time borrow, not ownership or a grant. The experiment retains the
// original Host, Bridge and HWNDs until actual SDK cleanup. The view is used
// only on engine, and never after a document change or close has started.
struct stage0_host_context final {
  engine_thread* engine{};
  HMODULE module{};
  const wkeCursorBrokerApi* broker{};
  wkeCursorBrokerRealm realm{};
  wkeCursorBrokerEndpoint endpoint{};
  wkeCursorBrokerIdentity identity{};
  wkeWebView view{};
  std::uint64_t context_receipt{};
};

} // namespace soui::sdk::detail
