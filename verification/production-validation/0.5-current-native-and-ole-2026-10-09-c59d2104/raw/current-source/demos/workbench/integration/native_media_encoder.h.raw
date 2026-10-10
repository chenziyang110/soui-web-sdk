#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <string_view>

namespace soui::demo {
struct media_encode_result {
  bool succeeded{false};
  bool cancelled{false};
  std::string message;
};
std::filesystem::path configured_media_encoder();
bool valid_media_profile(const std::string& format, const std::string& codec, unsigned quality);
// Operations are deliberately a small, audited vocabulary.  The Web side can
// select one of these names, but it cannot inject an FFmpeg filter expression.
bool valid_media_operation(std::string_view operation);
std::string media_output_extension(const std::string& format);
media_encode_result save_media_output(const std::filesystem::path& source,
    const std::filesystem::path& staging, const std::filesystem::path& destination,
    const std::atomic<bool>& cancel, const std::function<void(unsigned)>& progress);
// Only fixed profiles and native-owned paths enter the child process. No shell.
media_encode_result encode_media(const std::filesystem::path& executable,
    const std::filesystem::path& source, const std::filesystem::path& output,
    const std::string& format, const std::string& codec, unsigned quality,
    const std::atomic<bool>& cancel, const std::function<void(unsigned)>& progress,
    std::string_view operation = {});
}
