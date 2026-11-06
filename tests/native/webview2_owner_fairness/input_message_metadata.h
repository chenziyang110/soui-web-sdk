#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>

namespace owner_fairness {
// Documented INPUT_MESSAGE_SOURCE ABI (two 32-bit enums). Keep this diagnostic
// optional: the consumer retains WINVER/_WIN32_WINNT=0x0601 and no Win8 import.
struct message_source_wire { std::uint32_t device{},origin{}; };
static_assert(sizeof(message_source_wire)==8,"INPUT_MESSAGE_SOURCE ABI");
static_assert(alignof(message_source_wire)==4&&offsetof(message_source_wire,origin)==4,"INPUT_MESSAGE_SOURCE enum layout");
struct message_source_sample {
  bool attempted{},ok{};
  DWORD error{};
  message_source_wire value{};
};
class message_source_probe {
 public:
  void initialize() {
    const auto module=GetModuleHandleW(L"user32.dll");
    wchar_t path[32768]{};
    const auto size=module?GetModuleFileNameW(module,path,32768):0;
    if(size&&size<32768)module_path_.assign(path,size);
    SetLastError(ERROR_SUCCESS);
    query_=module?reinterpret_cast<query_fn>(GetProcAddress(module,"GetCurrentInputMessageSource")):nullptr;
    resolve_error_=query_?ERROR_SUCCESS:GetLastError();
    if(!module)resolve_error_=ERROR_MOD_NOT_FOUND;
  }
  message_source_sample sample() const noexcept {
    message_source_sample s{};
    if(!query_)return s;
    s.attempted=true;SetLastError(ERROR_SUCCESS);
    s.ok=query_(&s.value)!=FALSE;
    if(!s.ok)s.error=GetLastError();
    return s;
  }
  bool available() const noexcept{return query_!=nullptr;}
  DWORD resolve_error() const noexcept{return resolve_error_;}
  const std::wstring& module_path() const noexcept{return module_path_;}
 private:
  using query_fn=BOOL(WINAPI*)(message_source_wire*);
  query_fn query_{};
  DWORD resolve_error_{};
  std::wstring module_path_;
};
}
