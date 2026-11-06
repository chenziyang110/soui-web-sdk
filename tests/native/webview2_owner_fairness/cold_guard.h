#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <type_traits>
#include <json.hpp>
namespace r42_cold {
template<class T>struct observed {bool queried{},valid{};T value{};};
struct snapshot {
 const char* checkpoint{};const char* first_failed{};bool complete{},foreground_required{};
 std::uint64_t expected_owner{},expected_strip{};DWORD expected_pid{},expected_thread{};POINT expected_origin{};
 observed<DWORD> owner_tid,owner_pid,strip_tid,strip_pid,process_wait,load_wait,current_pid,own_session,target_session;
 observed<SHORT> escape;
 observed<BOOL> owner_window,strip_window,owner_visible,strip_visible,owner_iconic,owner_rect_result,strip_rect_result,owner_origin_result,strip_origin_result,own_session_result,target_session_result;
 observed<HWND> parent,root,foreground;
 observed<int> owner_class_result,strip_class_result;
 observed<std::array<wchar_t,80>> owner_class,strip_class;
 observed<RECT> owner_rect,strip_rect;
 observed<POINT> owner_origin,strip_origin;
};
struct state {std::array<snapshot,3> rows{};unsigned count{};};
template<class T>inline T save(observed<T>&o,T v){o.queried=o.valid=true;o.value=v;return v;}
template<class T>inline void output(observed<T>&o,const T&v,bool ok){o.queried=true;o.valid=ok;if(ok)o.value=v;}
inline nlohmann::json raw(RECT r){return {r.left,r.top,r.right,r.bottom};}
inline nlohmann::json raw(POINT p){return {p.x,p.y};}
inline nlohmann::json raw(const std::array<wchar_t,80>&a){auto j=nlohmann::json::array();for(auto c:a){if(!c)break;j.push_back(static_cast<std::uint16_t>(c));}return j;}
template<class T>inline nlohmann::json raw(T v){if constexpr(std::is_pointer_v<T>)return std::to_string(reinterpret_cast<std::uintptr_t>(v));else return v;}
template<class T>inline nlohmann::json field(const observed<T>&o){return {{"queried",o.queried},{"valueValid",o.valid},{"value",o.valid?raw(o.value):nlohmann::json(nullptr)}};}
inline nlohmann::json json(const state&s){auto rows=nlohmann::json::array();for(unsigned i=0;i<s.count;++i){const auto&r=s.rows[i];nlohmann::json values;
#define R42_FIELD(n) values[#n]=field(r.n)
 R42_FIELD(owner_tid);R42_FIELD(owner_pid);R42_FIELD(strip_tid);R42_FIELD(strip_pid);R42_FIELD(process_wait);R42_FIELD(load_wait);R42_FIELD(current_pid);R42_FIELD(own_session);R42_FIELD(target_session);R42_FIELD(escape);
 R42_FIELD(owner_window);R42_FIELD(strip_window);R42_FIELD(owner_visible);R42_FIELD(strip_visible);R42_FIELD(owner_iconic);R42_FIELD(owner_rect_result);R42_FIELD(strip_rect_result);R42_FIELD(owner_origin_result);R42_FIELD(strip_origin_result);R42_FIELD(own_session_result);R42_FIELD(target_session_result);
 R42_FIELD(parent);R42_FIELD(root);R42_FIELD(foreground);R42_FIELD(owner_class_result);R42_FIELD(strip_class_result);R42_FIELD(owner_class);R42_FIELD(strip_class);R42_FIELD(owner_rect);R42_FIELD(strip_rect);R42_FIELD(owner_origin);R42_FIELD(strip_origin);
#undef R42_FIELD
 rows.push_back({{"checkpoint",r.checkpoint},{"complete",r.complete},{"firstFailedPredicate",r.first_failed?nlohmann::json(r.first_failed):nlohmann::json(nullptr)},{"foregroundRequired",r.foreground_required},
  {"expected",{{"ownerHwnd",std::to_string(r.expected_owner)},{"stripHwnd",std::to_string(r.expected_strip)},{"pid",r.expected_pid},{"thread",r.expected_thread},{"storedStripOrigin",raw(r.expected_origin)}}},{"values",values},{"foregroundPid",nullptr},{"foregroundPidReason","not queried by original guard"}});
 }return {{"schema","soui.r42-cold-guard-diagnostics/v1"},{"capacity",3},{"count",s.count},{"rows",rows},{"unqueriedValuesRemainUnknown",true},{"failureTimeRequery",false}};}
}
