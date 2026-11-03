#pragma once

#include <cerrno>
#include <cstddef>

namespace soui::runtime::detail {

// Returns false unless every byte came from the platform CSPRNG. Callers must
// discard the entire output on failure; no weak or device-file fallback exists.
[[nodiscard]] bool secure_random(unsigned char* bytes, std::size_t size) noexcept;

// Private syscall boundary, also exercised with deterministic fault readers.
// A reader returns a byte count, or a negative result with errno set.
template <typename Reader>
bool fill_entropy_from_reader(unsigned char* bytes, std::size_t size, Reader&& read) noexcept {
  if (!bytes && size) return false;
  // Small getrandom requests avoid the large-read interruption behavior. Even
  // unexpected repeated EINTR must fail closed rather than spin indefinitely.
  unsigned interrupted_retries = 8;
  while (size) {
    const auto requested = size < 256U ? size : 256U;
    const auto count = read(bytes, requested);
    if (count < 0) {
      if (errno == EINTR && interrupted_retries) {
        --interrupted_retries;
        continue;
      }
      return false;
    }
    if (!count || static_cast<std::size_t>(count) > requested) return false;
    bytes += static_cast<std::size_t>(count);
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

}  // namespace soui::runtime::detail
