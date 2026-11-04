#pragma once
#include "support.h"

namespace perf {
inline json native_clock_anchor_json(std::uint64_t before,std::uint64_t utc,std::uint64_t after,
                                     std::uint64_t frequency,bool precise){
  if(!before||after<before||!frequency||!utc)return {{"available",false},{"reason","clock-read-failed-or-nonmonotonic"}};
  return {{"available",true},{"qpcBefore",std::to_string(before)},{"qpcAfter",std::to_string(after)},
    {"qpcFrequency",std::to_string(frequency)},{"utcFileTime100ns",std::to_string(utc)},
    {"readBracketQpcTicks",std::to_string(after-before)},
    {"utcMethod",precise?"GetSystemTimePreciseAsFileTime":"GetSystemTimeAsFileTime"},
    {"utcResolution100ns",nullptr},{"wallClockMayAdjust",true},{"rendererClockCalibration",false}};
}
inline json capture_native_clock_anchor(){
  using precise_clock=VOID (WINAPI*)(LPFILETIME);
  static const auto precise=reinterpret_cast<precise_clock>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetSystemTimePreciseAsFileTime"));
  LARGE_INTEGER frequency{},before{},after{};FILETIME utc{};
  if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0||!QueryPerformanceCounter(&before)||before.QuadPart<=0)
    return native_clock_anchor_json(0,0,0,0,false);
  if(precise)precise(&utc);else GetSystemTimeAsFileTime(&utc);
  if(!QueryPerformanceCounter(&after)||after.QuadPart<before.QuadPart)return native_clock_anchor_json(0,0,0,0,false);
  const auto filetime=(std::uint64_t(utc.dwHighDateTime)<<32)|utc.dwLowDateTime;
  return native_clock_anchor_json(static_cast<std::uint64_t>(before.QuadPart),filetime,
    static_cast<std::uint64_t>(after.QuadPart),static_cast<std::uint64_t>(frequency.QuadPart),precise!=nullptr);
}
}
