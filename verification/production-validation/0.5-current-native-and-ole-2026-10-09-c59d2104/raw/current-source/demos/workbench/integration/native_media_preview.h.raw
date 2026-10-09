#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace soui::demo {

struct media_preview_result {
  bool supported{false};
  std::string frame_data;
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint64_t duration_ms{0};
  std::string decoder;
  std::string message;
};

struct media_decode_result {
  bool supported{false};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t frames{0};
  double decode_fps{0.0};
  std::uint64_t checksum{0};
  std::string decoder;
  std::string message;
};

// Decode one bounded preview frame through Windows Media Foundation. The
// function never owns a long-lived decoder; it is intentionally a small,
// cancellable capability adapter for the UI preview path. Continuous playback
// and hardware texture queues remain separate media-engine work.
[[nodiscard]] bool media_preview_available() noexcept;
[[nodiscard]] media_preview_result decode_media_preview(const std::filesystem::path& path);
[[nodiscard]] media_decode_result measure_media_decode(const std::filesystem::path& path,
                                                       std::uint32_t max_frames);

}  // namespace soui::demo
