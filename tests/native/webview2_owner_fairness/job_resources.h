#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <json.hpp>

namespace owner_fairness {
// Measurement-only, existing mark2/mark3 Bridge callbacks only. No job mutation,
// handle discovery, polling, thread, process enumeration or input-loop work.
struct job_query_status {
  bool attempted{},succeeded{},begin_known{},end_known{};
  DWORD error{};
  std::uint64_t begin{},end{};
};
struct job_resource_sample {
  job_query_status basic,extended;
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
};
inline void job_resource_qpc(std::uint64_t& value,bool& known) noexcept {
  LARGE_INTEGER q{};known=QueryPerformanceCounter(&q)!=FALSE&&q.QuadPart>0;
  if(known)value=static_cast<std::uint64_t>(q.QuadPart);
}
inline job_resource_sample query_immediate_job_resources() noexcept {
  job_resource_sample s{};
  s.basic.attempted=true;job_resource_qpc(s.basic.begin,s.basic.begin_known);
  s.basic.succeeded=QueryInformationJobObject(nullptr,JobObjectBasicAccountingInformation,
    &s.accounting,sizeof(s.accounting),nullptr)!=FALSE;
  if(!s.basic.succeeded)s.basic.error=GetLastError(); // Before any other API.
  job_resource_qpc(s.basic.end,s.basic.end_known);
  s.extended.attempted=true;job_resource_qpc(s.extended.begin,s.extended.begin_known);
  s.extended.succeeded=QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,
    &s.limits,sizeof(s.limits),nullptr)!=FALSE;
  if(!s.extended.succeeded)s.extended.error=GetLastError();
  job_resource_qpc(s.extended.end,s.extended.end_known);
  return s;
}
inline nlohmann::json job_query_json(const job_query_status& s) {
  using json=nlohmann::json;
  return {{"attempted",s.attempted},{"succeeded",s.succeeded},
    {"win32Error",s.attempted&&!s.succeeded?json(s.error):json(nullptr)},
    {"beginQpc",s.begin_known?json(std::to_string(s.begin)):json(nullptr)},
    {"endQpc",s.end_known?json(std::to_string(s.end)):json(nullptr)}};
}
inline nlohmann::json job_resource_json(const job_resource_sample& s,unsigned stage) {
  using json=nlohmann::json;const bool b=s.basic.attempted&&s.basic.succeeded,
    e=s.extended.attempted&&s.extended.succeeded;
  auto basic=job_query_json(s.basic),extended=job_query_json(s.extended);
  basic["TotalUserTime100ns"]=b?json(std::to_string(s.accounting.TotalUserTime.QuadPart)):json(nullptr);
  basic["TotalKernelTime100ns"]=b?json(std::to_string(s.accounting.TotalKernelTime.QuadPart)):json(nullptr);
  basic["TotalProcesses"]=b?json(s.accounting.TotalProcesses):json(nullptr);
  basic["ActiveProcesses"]=b?json(s.accounting.ActiveProcesses):json(nullptr);
  basic["TotalTerminatedProcesses"]=b?json(s.accounting.TotalTerminatedProcesses):json(nullptr);
  extended["PeakJobMemoryUsedBytes"]=e?json(std::to_string(static_cast<std::uint64_t>(s.limits.PeakJobMemoryUsed))):json(nullptr);
  return {{"stage",stage},{"basicAccounting",basic},{"extendedLimits",extended}};
}
} // namespace owner_fairness
