#include "../../sdk/src/desktop/backends/linux_runtime_preflight.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using soui::desktop::detail::check_webkit_helpers;
int failures = 0;

#define CHECK(condition) do { if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
  } } while (false)

struct fixture {
  fs::path root;
  fixture() {
    auto pattern = (fs::temp_directory_path() / "soui-webkit-preflight-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const auto* created = ::mkdtemp(buffer.data());
    if (!created) throw std::runtime_error("Unable to create preflight fixture");
    root = created;
  }
  ~fixture() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error) {
      ++failures;
      std::cerr << "Unable to clean preflight fixture\n";
    }
  }
  void file(const char* name, bool executable) {
    const auto path = root / name;
    std::ofstream output(path);
    output << "This test fixture must never be executed.\n";
    output.close();
    if (!output) throw std::runtime_error("Unable to write preflight fixture");
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write |
                             (executable ? fs::perms::owner_exec : fs::perms::none));
  }
};

void rejected(std::string_view directory) {
  const auto result = check_webkit_helpers(directory);
  CHECK(!result.ok);
  CHECK(result.code == "E_WEBKIT_RUNTIME");
  CHECK(result.message.find("SOUI_DESKTOP_WEBKIT_HELPER_DIR") != std::string::npos);
  if (!directory.empty())
    CHECK(result.message.find(directory) == std::string::npos);
}

void cases() {
  const auto unknown = check_webkit_helpers("");
  CHECK(unknown.ok);
  CHECK(unknown.code.empty());
  rejected("relative-helper-directory");
  fixture files;
  const auto directory = files.root.string();
  rejected(directory);
  rejected((files.root / "missing-directory").string());
  files.file("WebKitNetworkProcess", true);
  rejected(directory);
  files.file("WebKitWebProcess", false);
  rejected(directory);
  fs::remove(files.root / "WebKitWebProcess");
  fs::create_directory(files.root / "WebKitWebProcess");
  rejected(directory);
  fs::remove(files.root / "WebKitWebProcess");
  files.file("WebKitWebProcess", true);
  CHECK(check_webkit_helpers(directory).ok);
  files.file("WebKitNetworkProcess", false);
  rejected(directory);
  files.file("WebKitNetworkProcess", true);
  fs::rename(files.root / "WebKitWebProcess", files.root / "real-web-process");
  fs::create_symlink("real-web-process", files.root / "WebKitWebProcess");
  CHECK(check_webkit_helpers(directory).ok);
  fs::remove(files.root / "real-web-process");
  rejected(directory);
  const auto embedded_null = directory + '\0' + "ignored";
  rejected(embedded_null);
}
}  // namespace

int main() {
  try {
    cases();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "Preflight fixture failure: " << error.what() << '\n';
  }
  if (failures) return 1;
  std::cout << "WebKitGTK helper preflight checks passed\n";
  return 0;
}
