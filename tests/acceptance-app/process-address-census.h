#pragma once
#include <windows.h>
#include <json.hpp>
#include <cstdint>
#include <limits>
#include <string>

namespace soui::acceptance::address_census {
constexpr std::uint32_t maximum_queries = 32768;
constexpr std::uint32_t maximum_milliseconds = 50;
struct result final {
  const char* state{"unavailable"};
  DWORD error{}, process_id{}, thread_id{};
  std::uint64_t begin_qpc{}, end_qpc{}, frequency{}, minimum{}, maximum{}, next_address{};
  std::uint64_t private_bytes{}, mapped_bytes{}, image_bytes{}, reserved_bytes{}, free_bytes{};
  std::uint32_t queries{}, private_regions{}, mapped_regions{}, image_regions{};
};
namespace detail {
// Injectable only for internal collector tests; production uses the real APIs.
struct api final {
  decltype(&VirtualQuery) query{&VirtualQuery};
  decltype(&QueryPerformanceCounter) counter{&QueryPerformanceCounter};
};
inline result collect(std::uintptr_t minimum, std::uintptr_t maximum,
                      std::uint64_t frequency, const api& calls = {}) noexcept {
  result out; out.process_id=GetCurrentProcessId(); out.thread_id=GetCurrentThreadId();
  out.minimum=minimum; out.maximum=maximum; out.next_address=minimum; out.frequency=frequency;
  if(!minimum || maximum<minimum || maximum==(std::numeric_limits<std::uint64_t>::max)() ||
     !frequency || frequency>static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()) ||
     !calls.query || !calls.counter) { out.state="invalid-range"; return out; }
  LARGE_INTEGER started{};
  if(!calls.counter(&started) || started.QuadPart<=0) { out.state="clock-error"; return out; }
  out.begin_qpc=out.end_qpc=static_cast<std::uint64_t>(started.QuadPart);
  const auto end=static_cast<std::uint64_t>(maximum)+1;
  const auto within_time=[&]() noexcept {
    LARGE_INTEGER now{};
    if(!calls.counter(&now) || now.QuadPart<started.QuadPart ||
       static_cast<std::uint64_t>(now.QuadPart)<out.end_qpc) { out.state="clock-error"; return false; }
    out.end_qpc=static_cast<std::uint64_t>(now.QuadPart);
    // floor(frequency * 50 / 1000), without a potentially overflowing product.
    if(out.end_qpc-out.begin_qpc>frequency/20) { out.state="time-budget"; return false; }
    return true;
  };
  while(out.next_address<end) {
    if(out.queries==maximum_queries) { out.state="region-budget"; return out; }
    if(!within_time())return out;
    MEMORY_BASIC_INFORMATION region{}; ++out.queries;
    const auto returned=calls.query(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(out.next_address)),&region,sizeof(region));
    const auto error=returned?ERROR_SUCCESS:GetLastError();
    if(!within_time()) { out.error=error; return out; }
    if(returned!=sizeof(region)) { out.state="query-error"; out.error=error; return out; }
    const auto base=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(region.BaseAddress));
    const auto size=static_cast<std::uint64_t>(region.RegionSize);
    if(!size || size>(std::numeric_limits<std::uint64_t>::max)()-base ||
       base>out.next_address || base+size<=out.next_address) { out.state="invalid-region"; return out; }
    const auto next=(base+size<end)?base+size:end;
    const auto bytes=next-out.next_address; // Clip an overlapping or final region; never double count it.
    if(region.State==MEM_FREE)out.free_bytes+=bytes;
    else if(region.State==MEM_RESERVE)out.reserved_bytes+=bytes;
    else if(region.State==MEM_COMMIT) {
      if(region.Type==MEM_PRIVATE) { out.private_bytes+=bytes; ++out.private_regions; }
      else if(region.Type==MEM_MAPPED) { out.mapped_bytes+=bytes; ++out.mapped_regions; }
      else if(region.Type==MEM_IMAGE) { out.image_bytes+=bytes; ++out.image_regions; }
      else { out.state="unknown-type"; return out; }
    } else { out.state="unknown-state"; return out; }
    out.next_address=next;
  }
  out.state="complete"; return out;
}
} // namespace detail
inline result capture() noexcept {
  SYSTEM_INFO system{}; GetSystemInfo(&system); LARGE_INTEGER frequency{};
  if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) {
    result out; out.state="clock-error"; out.process_id=GetCurrentProcessId(); out.thread_id=GetCurrentThreadId(); return out;
  }
  return detail::collect(reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress),
    reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress),static_cast<std::uint64_t>(frequency.QuadPart));
}
inline nlohmann::json serialize(const result& value) {
  const auto committed=value.private_bytes+value.mapped_bytes+value.image_bytes;
  const auto covered=committed+value.reserved_bytes+value.free_bytes;
  return {{"schemaVersion",1},{"scope","current-process-virtual-address-regions"},{"state",value.state},
    {"nonAtomic",true},{"physicalMemory",false},{"copyOnWriteReclassified",false},{"processId",std::to_string(value.process_id)},
    {"captureThreadId",std::to_string(value.thread_id)},{"pointerBytes",sizeof(void*)},{"win32Error",value.error},
    {"queryLimit",maximum_queries},{"timeLimitMs",maximum_milliseconds},{"queries",value.queries},
    {"beginQpc",std::to_string(value.begin_qpc)},{"endQpc",std::to_string(value.end_qpc)},{"qpcFrequency",std::to_string(value.frequency)},
    {"minimumAddress",std::to_string(value.minimum)},{"maximumAddress",std::to_string(value.maximum)},
    {"nextAddress",std::to_string(value.next_address)},{"coveredBytes",std::to_string(covered)},
    {"committedPrivateBytes",std::to_string(value.private_bytes)},{"committedMappedBytes",std::to_string(value.mapped_bytes)},
    {"committedImageBytes",std::to_string(value.image_bytes)},{"committedTotalBytes",std::to_string(committed)},
    {"reservedBytes",std::to_string(value.reserved_bytes)},{"freeBytes",std::to_string(value.free_bytes)},
    {"committedPrivateRegions",value.private_regions},{"committedMappedRegions",value.mapped_regions},{"committedImageRegions",value.image_regions}};
}
} // namespace soui::acceptance::address_census
