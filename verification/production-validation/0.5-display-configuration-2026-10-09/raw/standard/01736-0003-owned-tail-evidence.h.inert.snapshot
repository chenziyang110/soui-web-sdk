#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace soui::benchmark {
// Disk-object evidence only. This class never grants process ownership and never
// terminates anything; cleanup remains the enclosing held Job's responsibility.
class owned_tail_evidence final {
public:
    using writer = std::function<void(const std::string&)>;
    owned_tail_evidence(const std::wstring& path, const std::wstring& sha256,
                        const std::wstring& nonce, writer write);
    ~owned_tail_evidence();
    owned_tail_evidence(const owned_tail_evidence&) = delete;
    owned_tail_evidence& operator=(const owned_tail_evidence&) = delete;
    void before_cleanup(HANDLE job, bool eligible_reason);
    void after_cleanup(HANDLE job);
    std::string terminal_fields() const;
private:
    struct implementation;
    std::unique_ptr<implementation> implementation_;
};
} // namespace soui::benchmark
