// Standalone, no GUI or SDK link. The enabled fixture gets a generated config
// header; the disabled fixture deliberately has no config include directory.
#include "../../sdk/src/desktop/startup_trace.h"

#include <iostream>

namespace trace = SOUI_STARTUP_TRACE_NAMESPACE;
static_assert(noexcept(trace::mark(trace::stage::app_main_enter)));

#if defined(SOUI_STARTUP_TRACE_ENABLED) && SOUI_STARTUP_TRACE_ENABLED && \
    (defined(__APPLE__) || defined(SOUI_STARTUP_TRACE_TEST))
#include <csignal>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>

namespace {
constexpr const char* path = SOUI_STARTUP_TRACE_FILE;
constexpr const char* first_line = "SOUI_STARTUP_TRACE/1 app_main_enter\n";
int failures = 0;

#define CHECK(condition) do { if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
  } } while (false)

std::string read_file(const std::string& name = path) {
  std::ifstream input(name, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool create_file(const std::string& content = {}) {
  const int descriptor = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (descriptor < 0) return false;
  const bool good = ::fchmod(descriptor, 0600) == 0 &&
    ::write(descriptor, content.data(), content.size()) == static_cast<ssize_t>(content.size());
  return ::close(descriptor) == 0 && good;
}

template <typename Action>
void child(Action action) {
  // Each case starts with fresh, uninitialized process-local production state.
  // A watchdog makes accidental FIFO/lock blocking an explicit test failure.
  const auto process = ::fork();
  CHECK(process >= 0);
  if (process < 0) return;
  if (process == 0) {
    ::alarm(3);
    ::_exit(action() ? 0 : 1);
  }
  int status = 0;
  CHECK(::waitpid(process, &status, 0) == process);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void mark_first() {
  child([] {
    errno = ERANGE;
    trace::mark(trace::stage::app_main_enter);
    return errno == ERANGE;
  });
}

bool available_lock() {
  const int descriptor = ::open(path, O_WRONLY | O_CLOEXEC);
  if (descriptor < 0) return false;
  const bool available = ::flock(descriptor, LOCK_EX | LOCK_NB) == 0;
  ::close(descriptor);
  return available;
}

void cases() {
  // Never create a missing destination; a rejected sink stays disabled.
  child([] {
    trace::mark(trace::stage::app_main_enter);
    if (!create_file()) return false;
    trace::mark(trace::stage::app_exit);
    return read_file().empty();
  });
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  child([] {
    trace::mark(static_cast<trace::stage>(std::numeric_limits<unsigned>::max()));
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
      for (unsigned i = 0; i <= static_cast<unsigned>(trace::stage::dialog_inactive_skip); ++i)
        trace::mark(static_cast<trace::stage>(i));
    }
    return !available_lock();  // The sink owns its lock until process exit.
  });
  const auto all = read_file();
  CHECK(all.rfind(first_line, 0) == 0);
  CHECK(all.find(first_line, std::string(first_line).size()) == std::string::npos);
  CHECK(all.size() < 16 * 1024);
  std::size_t lines = 0;
  for (std::size_t position = 0; position < all.size();) {
    CHECK(all.compare(position, sizeof("SOUI_STARTUP_TRACE/1 ") - 1,
      "SOUI_STARTUP_TRACE/1 ") == 0);
    const auto end = all.find('\n', position);
    CHECK(end != std::string::npos);
    if (end == std::string::npos) break;
    ++lines;
    position = end + 1;
  }
  CHECK(lines == 43);
  CHECK(available_lock());
  CHECK(::unlink(path) == 0);

  CHECK(create_file("existing\n"));
  mark_first();
  CHECK(read_file() == "existing\n");
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  CHECK(::chmod(path, 0640) == 0);
  mark_first();
  CHECK(read_file().empty());
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  const std::string other = std::string(path) + ".other";
  CHECK(::rename(path, other.c_str()) == 0);
  CHECK(::symlink(other.c_str(), path) == 0);
  mark_first();
  CHECK(read_file(other).empty());
  CHECK(::unlink(path) == 0);
  CHECK(::link(other.c_str(), path) == 0);
  mark_first();
  CHECK(read_file(other).empty());
  CHECK(::unlink(path) == 0);
  CHECK(::unlink(other.c_str()) == 0);

  CHECK(::mkfifo(path, 0600) == 0);
  mark_first();
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  const int lock = ::open(path, O_WRONLY | O_CLOEXEC);
  CHECK(lock >= 0);
  CHECK(::flock(lock, LOCK_EX | LOCK_NB) == 0);
  mark_first();
  CHECK(read_file().empty());
  CHECK(::close(lock) == 0);
  CHECK(::unlink(path) == 0);

  // Permission, size, and pathname identity are rechecked between marks.
  CHECK(create_file());
  child([] {
    trace::mark(trace::stage::app_main_enter);
    if (::chmod(path, 0640) != 0) return false;
    trace::mark(trace::stage::app_exit);
    if (::chmod(path, 0600) != 0) return false;
    trace::mark(trace::stage::app_run_enter);
    return read_file() == first_line && available_lock();
  });
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  child([] {
    trace::mark(trace::stage::app_main_enter);
    if (::truncate(path, 16 * 1024) != 0) return false;
    trace::mark(trace::stage::app_exit);
    return read_file().size() == 16 * 1024 && available_lock();
  });
  CHECK(::unlink(path) == 0);

  CHECK(create_file());
  child([&] {
    trace::mark(trace::stage::app_main_enter);
    if (::rename(path, other.c_str()) != 0 || !create_file()) return false;
    trace::mark(trace::stage::app_exit);
    return read_file().empty() && read_file(other) == first_line && available_lock();
  });
  CHECK(::unlink(path) == 0);
  CHECK(::unlink(other.c_str()) == 0);

  // A short write must disable/close the sink without retrying or repairing it.
  CHECK(create_file());
  child([] {
    const struct rlimit limit{5, 5};
    if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) return false;
    std::signal(SIGXFSZ, SIG_IGN);
    trace::mark(trace::stage::app_main_enter);
    trace::mark(trace::stage::app_exit);
    return read_file() == std::string(first_line, 5) && available_lock();
  });
  CHECK(::unlink(path) == 0);
}
}  // namespace

int main() {
  struct stat existing{};
  if (::lstat(path, &existing) == 0 || errno != ENOENT ||
      ::lstat((std::string(path) + ".other").c_str(), &existing) == 0 || errno != ENOENT) {
    std::cerr << "Trace fixture destinations must not already exist.\n";
    return 1;
  }
  cases();
  if (failures != 0) return 1;
  std::cout << "startup trace policy: passed\n";
  return 0;
}
#else
int main() {
  for (unsigned i = 0; i < 100; ++i) trace::mark(static_cast<trace::stage>(i));
  std::cout << "startup trace disabled: passed\n";
  return 0;
}
#endif
