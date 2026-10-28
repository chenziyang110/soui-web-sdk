#ifndef SOUI_BENCHMARK_OWNED_PROCESS_COHORT_H
#define SOUI_BENCHMARK_OWNED_PROCESS_COHORT_H

#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace soui::benchmark {
namespace cohort_detail {
constexpr std::size_t member_limit = 256;
enum class truth { unknown, no, yes };

class held_processes final {
public:
    held_processes() noexcept = default;
    ~held_processes() {
        for (const auto value : values_) if (value) CloseHandle(value);
    }
    held_processes(const held_processes&) = delete;
    held_processes& operator=(const held_processes&) = delete;
    void set(std::size_t index, HANDLE value) noexcept { values_[index] = value; }
    HANDLE get(std::size_t index) const noexcept { return values_[index]; }
private:
    std::array<HANDLE, member_limit> values_{};
};

struct accounting_sample final {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
    bool observed = false;
    DWORD error = ERROR_SUCCESS;
};
inline accounting_sample query_accounting(HANDLE job) noexcept {
    accounting_sample out;
    out.observed = QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
        &out.value, sizeof(out.value), nullptr) != FALSE;
    if (!out.observed) out.error = GetLastError();
    return out;
}
inline bool same_accounting(const accounting_sample& a, const accounting_sample& b) noexcept {
    return a.observed && b.observed && a.value.ActiveProcesses == b.value.ActiveProcesses &&
        a.value.TotalProcesses == b.value.TotalProcesses &&
        a.value.TotalTerminatedProcesses == b.value.TotalTerminatedProcesses;
}
inline const char* json_truth(truth value) noexcept {
    return value == truth::yes ? "true" : value == truth::no ? "false" : "null";
}
inline std::string count(bool observed, std::uint64_t value) {
    return observed ? std::to_string(value) : "null";
}
inline std::string accounting_json(const accounting_sample& value) {
    return std::string("{\"observed\":") + (value.observed ? "true" : "false") +
        ",\"activeProcesses\":" + count(value.observed, value.value.ActiveProcesses) +
        ",\"totalProcesses\":" + count(value.observed, value.value.TotalProcesses) +
        ",\"limitTerminatedProcesses\":" + count(value.observed, value.value.TotalTerminatedProcesses) +
        ",\"win32Error\":" + count(!value.observed, value.error) + "}";
}

struct member_sample final {
    std::uint64_t queriedPid = 0;
    DWORD pid = 0;
    std::uint64_t creation = 0;
    bool identityObserved = false;
    truth inJob = truth::unknown, live = truth::unknown;
    truth recheckInJob = truth::unknown, recheckLive = truth::unknown;
    const char* reason = nullptr;
    DWORD error = ERROR_SUCCESS;
    bool errorObserved = false;
    void unknown(const char* why, DWORD code, bool hasError = true) noexcept {
        if (!reason) { reason = why; error = code; errorObserved = hasError; }
    }
};
inline void membership(member_sample& member, HANDLE process, HANDLE job, bool again) noexcept {
    BOOL belongs = FALSE;
    auto& result = again ? member.recheckInJob : member.inJob;
    if (!IsProcessInJob(process, job, &belongs)) {
        member.unknown(again ? "membership-recheck-failed" : "membership-query-failed", GetLastError());
        return;
    }
    result = belongs ? truth::yes : truth::no;
    if (!belongs) member.unknown("member-no-longer-owned", ERROR_SUCCESS, false);
}
inline void liveness(member_sample& member, HANDLE process, bool again) noexcept {
    auto& result = again ? member.recheckLive : member.live;
    const auto waited = WaitForSingleObject(process, 0);
    if (waited == WAIT_TIMEOUT) result = truth::yes;
    else if (waited == WAIT_OBJECT_0) {
        result = truth::no;
        member.unknown("member-exited", ERROR_SUCCESS, false);
    } else member.unknown("member-live-query-failed", waited == WAIT_FAILED ? GetLastError() : waited);
}
inline void inspect_member(member_sample& member, HANDLE process, HANDLE job) noexcept {
    membership(member, process, job, false);
    if (member.inJob != truth::yes) return;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        member.creation = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        member.identityObserved = member.creation != 0;
        if (!member.identityObserved) member.unknown("invalid-member-birth", ERROR_SUCCESS, false);
    } else member.unknown("member-birth-query-failed", GetLastError());
    liveness(member, process, false);
}
inline std::string member_json(const member_sample& value) {
    return "{\"queriedPid\":\"" + std::to_string(value.queriedPid) +
        "\",\"pid\":" + count(value.pid != 0, value.pid) + ",\"creationFileTime\":" +
        (value.identityObserved ? "\"" + std::to_string(value.creation) + "\"" : "null") +
        ",\"inJob\":" + json_truth(value.inJob) + ",\"live\":" + json_truth(value.live) +
        ",\"recheckInJob\":" + json_truth(value.recheckInJob) +
        ",\"recheckLive\":" + json_truth(value.recheckLive) +
        ",\"reason\":" + (value.reason ? "\"" + std::string(value.reason) + "\"" : "null") +
        ",\"win32Error\":" + count(value.errorObserved, value.error) + "}";
}
struct pid_buffer final {
    DWORD NumberOfAssignedProcesses = 0;
    DWORD NumberOfProcessIdsInList = 0;
    ULONG_PTR ProcessIdList[member_limit]{};
};
static_assert(offsetof(pid_buffer, ProcessIdList) == offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList));

template<class Clock>
std::string collect(HANDLE job, DWORD subjectPid, std::uint64_t subjectCreation,
    std::uint64_t id, const char* latchedFault, Clock elapsed) {
    const auto beginTick = GetTickCount64();
    const auto beginElapsed = elapsed();
    const char* reason = latchedFault;
    const auto note = [&reason](const char* why) { if (!reason) reason = why; };
    const auto before = query_accounting(job);
    if (!before.observed) note("accounting-before-failed");
    pid_buffer ids;
    bool enumerated = false;
    DWORD enumerationError = ERROR_SUCCESS;
    std::size_t memberCount = 0;
    std::array<member_sample, member_limit> members{};
    held_processes held;
    if (before.observed && !latchedFault) {
        enumerated = QueryInformationJobObject(job, JobObjectBasicProcessIdList,
            &ids, sizeof(ids), nullptr) != FALSE;
        if (!enumerated) {
            enumerationError = GetLastError();
            note(enumerationError == ERROR_MORE_DATA ? "member-limit" : "pid-list-query-failed");
        } else if (ids.NumberOfProcessIdsInList > member_limit) note("invalid-pid-list-size");
        else {
            memberCount = ids.NumberOfProcessIdsInList;
            if (ids.NumberOfAssignedProcesses != memberCount) note("pid-list-incomplete");
            for (std::size_t i = 0; i < memberCount; ++i) {
                auto& member = members[i];
                member.queriedPid = static_cast<std::uint64_t>(ids.ProcessIdList[i]);
                if (!member.queriedPid || member.queriedPid > (std::numeric_limits<DWORD>::max)()) {
                    member.unknown("invalid-pid", ERROR_SUCCESS, false); note(member.reason); continue;
                }
                member.pid = static_cast<DWORD>(member.queriedPid);
                bool duplicate = false;
                for (std::size_t j = 0; j < i; ++j) if (members[j].pid == member.pid) duplicate = true;
                if (duplicate) {
                    member.unknown("duplicate-pid", ERROR_SUCCESS, false); note(member.reason); continue;
                }
                const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, member.pid);
                if (!process) {
                    member.unknown("member-open-failed", GetLastError()); note(member.reason); continue;
                }
                held.set(i, process);
                inspect_member(member, process, job);
                if (member.reason) note(member.reason);
            }
            // Every opened handle remains held through both identity/liveness checks and final accounting.
            for (std::size_t i = 0; i < memberCount; ++i) if (held.get(i)) {
                membership(members[i], held.get(i), job, true);
                liveness(members[i], held.get(i), true);
                if (members[i].reason) note(members[i].reason);
            }
        }
    }
    const auto after = query_accounting(job);
    if (!after.observed) note("accounting-after-failed");
    if (before.observed && after.observed && !same_accounting(before, after)) note("accounting-changed");
    if (enumerated && before.observed && after.observed &&
        (memberCount != before.value.ActiveProcesses || memberCount != after.value.ActiveProcesses ||
         ids.NumberOfAssignedProcesses != memberCount || before.value.TotalProcesses < before.value.ActiveProcesses))
        note("membership-count-mismatch");
    const auto endElapsed = elapsed();
    const auto endTick = GetTickCount64();
    if (endTick < beginTick || endElapsed < beginElapsed) note("query-clock-regressed");
    const bool complete = !reason && enumerated && before.observed && after.observed;
    std::string result = "{\"schema\":\"soui.owned-job-cohort/v1\",\"event\":\"owned-cohort\",\"sampleId\":" +
        (latchedFault ? std::string("null") : "\"" + std::to_string(id) + "\"") +
        ",\"subjectPid\":" + std::to_string(subjectPid) + ",\"subjectCreationFileTime\":\"" +
        std::to_string(subjectCreation) + "\",\"queryBeginTickMs\":\"" + std::to_string(beginTick) +
        "\",\"queryEndTickMs\":\"" + std::to_string(endTick) + "\",\"queryBeginElapsedMs\":\"" +
        std::to_string(beginElapsed) + "\",\"queryEndElapsedMs\":\"" + std::to_string(endElapsed) +
        "\",\"complete\":" + (complete ? "true" : "false") + ",\"status\":\"" +
        (complete ? "complete" : "unknown") + "\",\"reason\":" +
        (reason ? "\"" + std::string(reason) + "\"" : "null") +
        ",\"accountingBefore\":" + accounting_json(before) + ",\"accountingAfter\":" + accounting_json(after) +
        ",\"assignedProcesses\":" + count(enumerated, ids.NumberOfAssignedProcesses) +
        ",\"returnedProcesses\":" + count(enumerated, ids.NumberOfProcessIdsInList) +
        ",\"enumerationError\":" + count(before.observed && !latchedFault && !enumerated, enumerationError) +
        ",\"memberLimit\":256,\"members\":[";
    for (std::size_t i = 0; i < memberCount; ++i) {
        if (i) result += ',';
        result += member_json(members[i]);
    }
    result += "],\"outsideJobResidual\":null,\"borrowedResourceResidual\":null,\"authority\":\"observation-only\"}";
    return result;
}
} // namespace cohort_detail

// Borrows the original Job for this runner lifetime; never closes, mutates or exports it.
class owned_job_cohort_observer final {
public:
    owned_job_cohort_observer(HANDLE job, DWORD pid, std::uint64_t creation, std::uint64_t interval) noexcept
        : job_(job), pid_(pid), creation_(creation), interval_(interval) {}
    owned_job_cohort_observer(const owned_job_cohort_observer&) = delete;
    owned_job_cohort_observer& operator=(const owned_job_cohort_observer&) = delete;
    template<class Clock> std::string poll(Clock elapsed) noexcept {
        try {
            const auto now = elapsed();
            if (sampled_ && now < next_) return {};
            sampled_ = true;
            // Schedule even a failed attempt, so allocation failure cannot cause a catch-up loop.
            next_ = now <= (std::numeric_limits<std::uint64_t>::max)() - interval_
                ? now + interval_ : (std::numeric_limits<std::uint64_t>::max)();
            if (id_ >= (std::numeric_limits<std::uint64_t>::max)() - 1) fault_ = "sample-id-exhausted";
            else if (!fault_) ++id_;
            auto row = cohort_detail::collect(job_, pid_, creation_, id_, fault_, elapsed);
            const auto finished = elapsed();
            next_ = finished <= (std::numeric_limits<std::uint64_t>::max)() - interval_
                ? finished + interval_ : (std::numeric_limits<std::uint64_t>::max)();
            return row;
        } catch (...) {
            // Allocation/observer failure cannot alter original timeout/closure policy.
            // No event is not a successful sample; subsequent admitted observations latch unknown.
            fault_ = "observer-exception";
            return {};
        }
    }
private:
    HANDLE job_; // Borrowed; only query rights are exercised.
    DWORD pid_;
    std::uint64_t creation_, interval_, id_ = 0, next_ = 0;
    bool sampled_ = false;
    const char* fault_ = nullptr;
};
} // namespace soui::benchmark
#endif
