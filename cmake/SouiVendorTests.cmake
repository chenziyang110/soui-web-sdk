# SDK-owned tests stay enabled independently of private kernel source tests.
# A completely absent private root is a supported portable SDK source layout.
# An existing incomplete root must fail, rather than silently disabling tests.
get_filename_component(_soui_vendor_distribution_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_soui_vendor_root "${_soui_vendor_distribution_root}/third_party/sources/miniblink")
set(_soui_vendor_default OFF)
if(SOUI_BUILD_TESTS AND EXISTS "${_soui_vendor_root}")
  set(_soui_vendor_default ON)
endif()
option(SOUI_BUILD_VENDOR_TESTS "Build private Miniblink source regressions (all inputs required)" ${_soui_vendor_default})
if(SOUI_BUILD_VENDOR_TESTS AND NOT SOUI_BUILD_TESTS)
  message(FATAL_ERROR "SOUI_BUILD_VENDOR_TESTS=ON requires SOUI_BUILD_TESTS=ON")
endif()
# Update this preflight when a vendor source regression adds an input.
# Existing leaf source maps, raw hashes and generated-fragment gates remain authoritative.
set(_soui_vendor_required
  "third_party/sources/miniblink/content/browser/CheckReEnter.cpp"
  "third_party/sources/miniblink/content/browser/SharedTimerWin.h"
  "third_party/sources/miniblink/content/browser/WebPage.cpp"
  "third_party/sources/miniblink/content/browser/WebPageImpl.cpp"
  "third_party/sources/miniblink/content/ui/DragHandle.cpp"
  "third_party/sources/miniblink/content/web_impl_win/BlinkPlatformImpl.cpp"
  "third_party/sources/miniblink/content/web_impl_win/BlinkPlatformImpl.h"
  "third_party/sources/miniblink/content/web_impl_win/TaskSchedulingPolicy.h"
  "third_party/sources/miniblink/content/web_impl_win/WebSchedulerImpl.cpp"
  "third_party/sources/miniblink/content/web_impl_win/WebSchedulerImpl.h"
  "third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.cpp"
  "third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.h"
  "third_party/sources/miniblink/content/web_impl_win/WebTimerBase.cpp"
  "third_party/sources/miniblink/content/web_impl_win/WebTimerBase.h"
  "third_party/sources/miniblink/net/ActivatingObjCheck.cpp"
  "third_party/sources/miniblink/net/ActivatingObjCheck.h"
  "third_party/sources/miniblink/third_party/WebKit/Source/wtf/Threading.h"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebDragData.h"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebThread.h"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebVector.h"
  "third_party/sources/miniblink/wke/CursorDescriptorBroker.cpp"
  "third_party/sources/miniblink/wke/CursorDescriptorBroker.h"
  "third_party/sources/miniblink/wke/wke.cpp"
  "third_party/sources/miniblink/wke/wkeNetHook.cpp"
  "third_party/sources/miniblink/wke/wkeWebView.cpp"
  "third_party/sources/miniblink/wke/wke_cursor_broker.h"
  "third_party/sources/miniblink/wke/wkedefine.h"
)
if(SOUI_BUILD_VENDOR_TESTS)
  set(_soui_vendor_missing "")
  foreach(_soui_vendor_relative IN LISTS _soui_vendor_required)
    set(_soui_vendor_input "${_soui_vendor_distribution_root}/${_soui_vendor_relative}")
    if(NOT EXISTS "${_soui_vendor_input}" OR IS_DIRECTORY "${_soui_vendor_input}")
      list(APPEND _soui_vendor_missing "${_soui_vendor_relative}")
    endif()
  endforeach()
  if(_soui_vendor_missing)
    list(JOIN _soui_vendor_missing "\n  " _soui_vendor_missing_message)
    message(FATAL_ERROR "Requested private vendor tests lack required regular files:\n  ${_soui_vendor_missing_message}")
  endif()
  message(STATUS "Private vendor source regressions requested; existing source-byte gates still apply")
else()
  message(STATUS "Private vendor source regressions NOT registered (SOUI_BUILD_VENDOR_TESTS=OFF); vendor regression gate not evaluated")
endif()
unset(_soui_vendor_default)
unset(_soui_vendor_root)
unset(_soui_vendor_required)
unset(_soui_vendor_relative)
unset(_soui_vendor_input)
unset(_soui_vendor_missing)
unset(_soui_vendor_missing_message)
unset(_soui_vendor_distribution_root)
