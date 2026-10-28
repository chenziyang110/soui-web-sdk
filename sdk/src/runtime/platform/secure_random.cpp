#include "secure_random.h"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <limits>
#elif defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <Security/SecRandom.h>
#else
#error "The Runtime requires a supported system CSPRNG: Windows, Linux, or macOS"
#endif

namespace soui::runtime::detail {

bool secure_random(unsigned char* bytes, std::size_t size) noexcept {
  if (!size) return true;
  if (!bytes) return false;
#if defined(_WIN32)
  return size <= (std::numeric_limits<ULONG>::max)() &&
      BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(size),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__linux__)
  return fill_entropy_from_reader(bytes, size, [](unsigned char* output, std::size_t count) noexcept {
    // Do not wait for entropy during early boot or substitute an uninitialized
    // source. EAGAIN, ENOSYS, and all other permanent failures remain failures.
    return ::getrandom(output, count, GRND_NONBLOCK);
  });
#elif defined(__APPLE__) && defined(__MACH__)
  return SecRandomCopyBytes(kSecRandomDefault, size, bytes) == 0;
#endif
}

}  // namespace soui::runtime::detail
