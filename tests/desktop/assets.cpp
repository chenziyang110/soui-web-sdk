#include "../../sdk/src/desktop/assets.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace {
namespace fs = std::filesystem;
using soui::desktop::detail::asset_result;
using soui::desktop::detail::read_asset;

int checks = 0;
int failures = 0;
int skipped_links = 0;
std::string context;

#define CHECK(condition) do { ++checks; if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << " [" << context \
                       << "]: " #condition "\n"; } } while (false)

struct files {
  fs::path base, root, sibling;

  files() {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned attempt = 0; attempt != 100; ++attempt) {
      base = fs::temp_directory_path() /
          ("soui-desktop-assets-" + stamp + "-" + std::to_string(attempt));
      if (fs::create_directory(base)) break;
      if (attempt == 99) throw std::runtime_error("Cannot create isolated asset fixture");
    }
    try {
      // Reserved characters in the native root are not request-path syntax.
      root = base / fs::u8path(u8"resources space # % 中文");
      sibling = base / fs::u8path(root.filename().u8string() + "-other");
      fs::create_directory(root);
      fs::create_directory(sibling);
      fs::create_directory(root / "nested");
      write(root / "hello.txt", "hello\n");
      write(root / "nested" / "hello.txt", "inside");
      write(base / "outside.txt", "outside");
      write(sibling / "secret.txt", "prefix sibling");
    } catch (...) {
      std::error_code ignored;
      fs::remove_all(base, ignored);
      throw;
    }
  }

  ~files() {
    std::error_code ignored;
    fs::remove_all(base, ignored);
  }

  static void write(const fs::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("Cannot write asset fixture");
  }
};

void success(const asset_result& value, std::string_view mime,
             std::size_t size, std::string_view base64) {
  CHECK(value.status.ok);
  CHECK(value.status.code.empty());
  CHECK(value.mime == mime);
  CHECK(value.size == size);
  CHECK(value.base64 == base64);
}

void rejected(const asset_result& value, std::string_view code) {
  CHECK(!value.status.ok);
  CHECK(value.status.code == code);
  CHECK(!value.status.message.empty());
  CHECK(value.mime.empty());
  CHECK(value.base64.empty());
  CHECK(value.size == 0);
}

void bytes_and_mime(files& fixture) {
  context = "text bytes";
  success(read_asset(fixture.root, "hello.txt"), "text/plain", 6, "aGVsbG8K");
  context = "nested relative file";
  success(read_asset(fixture.root, "nested/hello.txt"), "text/plain", 6, "aW5zaWRl");

  const std::string unicode_path = u8"nested/héllo 中文 \U0001F642.txt";
  const std::string unicode_text = u8"héllo 中文 \U0001F642\n";
  files::write(fixture.root / fs::u8path(unicode_path), unicode_text);
  context = "strict UTF-8 accepts valid two, three and four-byte characters";
  success(read_asset(fixture.root, unicode_path), "text/plain",
          unicode_text.size(), "aMOpbGxvIOS4reaWhyDwn5mCCg==");

  struct vector {
    const char* text;
    const char* encoded;
  };
  const vector vectors[] = {
    {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
    {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}
  };
  for (const auto& vector : vectors) {
    context = "base64 standard vector length " + std::to_string(std::string_view(vector.text).size());
    files::write(fixture.root / "vector.bin", vector.text);
    success(read_asset(fixture.root, "vector.bin"), "application/octet-stream",
            std::string_view(vector.text).size(), vector.encoded);
  }

  std::string binary;
  for (unsigned byte = 0; byte != 256; ++byte) binary += static_cast<char>(byte);
  files::write(fixture.root / "all-bytes.bin", binary);
  context = "every byte including NUL, CR, LF and high-bit bytes";
  success(read_asset(fixture.root, "all-bytes.bin"), "application/octet-stream", 256,
      "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEy"
      "MzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2Rl"
      "ZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeY"
      "mZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrL"
      "zM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==");

  struct mime_case {
    const char* name;
    const char* mime;
  };
  const mime_case types[] = {
    {"page.HTML", "text/html"}, {"page.htm", "text/html"},
    {"style.CsS", "text/css"}, {"script.js", "text/javascript"},
    {"module.mjs", "text/javascript"}, {"config.JsOn", "application/json"},
    {"notes.txt", "text/plain"}, {"table.csv", "text/csv"},
    {"data.xml", "application/xml"}, {"drawing.SVG", "image/svg+xml"},
    {"image.png", "image/png"}, {"photo.jpg", "image/jpeg"},
    {"photo.jpeg", "image/jpeg"}, {"image.gif", "image/gif"},
    {"image.webp", "image/webp"}, {"icon.ico", "image/x-icon"},
    {"module.wasm", "application/wasm"}, {"document.pdf", "application/pdf"},
    {"font.woff", "font/woff"}, {"font.woff2", "font/woff2"},
    {"font.ttf", "font/ttf"}, {"font.otf", "font/otf"},
    {"sound.mp3", "audio/mpeg"}, {"sound.wav", "audio/wav"},
    {"sound.ogg", "audio/ogg"}, {"movie.mp4", "video/mp4"},
    {"movie.webm", "video/webm"}, {"unknown.xyz", "application/octet-stream"},
    {"no-extension", "application/octet-stream"}, {".hidden", "application/octet-stream"},
    {"page.html.unknown", "application/octet-stream"}
  };
  for (const auto& type : types) {
    context = std::string("extension-only MIME: ") + type.name;
    // Deliberately not valid content for these formats: MIME is not sniffed.
    files::write(fixture.root / type.name, "x");
    success(read_asset(fixture.root, type.name), type.mime, 1, "eA==");
  }
}

void size_boundaries(files& fixture) {
  constexpr std::size_t cap = 256 * 1024;
  context = "documented byte cap";
  CHECK(soui::desktop::detail::maximum_asset_bytes == cap);

  // "aaa" encodes as "YWFh"; this oracle is independent of the reader's encoder.
  std::string expected;
  for (std::size_t index = 0; index < (cap - 1) / 3; ++index) expected += "YWFh";
  files::write(fixture.root / "limit.bin", std::string(cap - 1, 'a'));
  context = "one byte below cap";
  success(read_asset(fixture.root, "limit.bin"), "application/octet-stream", cap - 1, expected);

  files::write(fixture.root / "limit.bin", std::string(cap, 'a'));
  context = "exactly 256 KiB";
  success(read_asset(fixture.root, "limit.bin"), "application/octet-stream", cap, expected + "YQ==");

  files::write(fixture.root / "limit.bin", std::string(cap + 1, 'a'));
  context = "one byte above cap";
  rejected(read_asset(fixture.root, "limit.bin"), "asset_too_large");
}

void invalid_paths(files& fixture) {
  struct path_case {
    const char* label;
    std::string path;
  };
  const path_case cases[] = {
    {"empty path", ""}, {"dot path", "."}, {"parent path", ".."},
    {"leading dot segment", "./hello.txt"}, {"leading parent segment", "../outside.txt"},
    {"interior parent segment", "nested/../hello.txt"},
    {"interior dot segment", "nested/./hello.txt"},
    {"final dot segment", "nested/."}, {"final parent segment", "nested/.."},
    {"empty middle segment", "nested//hello.txt"}, {"trailing slash", "nested/"},
    {"absolute POSIX path", "/hello.txt"}, {"UNC forward slash", "//server/share/hello.txt"},
    {"drive absolute", "C:/hello.txt"}, {"drive relative", "C:hello.txt"},
    {"backslash separator", "nested\\hello.txt"}, {"backslash root", "\\hello.txt"},
    {"UNC backslash", "\\\\server\\share\\hello.txt"},
    {"file URI", "file:///hello.txt"}, {"HTTPS URI", "https://example.invalid/hello.txt"},
    {"encoded parent traversal", "%2e%2e/outside.txt"},
    {"encoded separator", "nested%2Fhello.txt"}, {"encoded extension", "hello%2etxt"},
    {"literal percent", "100%.txt"}, {"NTFS stream", "hello.txt:secret"},
    {"query", "hello.txt?download=1"}, {"fragment", "hello.txt#section"},
    {"embedded NUL", std::string("hello.txt\0suffix", 16)},
    {"leading NUL", std::string("\0hello.txt", 10)},
    {"isolated continuation", std::string("\x80") + ".txt"},
    {"overlong two-byte encoding", std::string("\xC0\xAF") + ".txt"},
    {"overlong three-byte encoding", std::string("\xE0\x80\xAF") + ".txt"},
    {"overlong four-byte encoding", std::string("\xF0\x80\x80\xAF") + ".txt"},
    {"UTF-16 surrogate", std::string("\xED\xA0\x80") + ".txt"},
    {"above Unicode maximum", std::string("\xF4\x90\x80\x80") + ".txt"},
    {"invalid UTF-8 leading byte", std::string("\xFF") + ".txt"},
    {"invalid UTF-8 continuation", std::string("\xC2") + "x.txt"},
    {"truncated two-byte sequence", std::string("hello.txt\xC2")},
    {"truncated three-byte sequence", std::string("hello.txt\xE2\x82")},
    {"truncated four-byte sequence", std::string("hello.txt\xF0\x9F\x99")},
    {"path over 4096 UTF-8 bytes", std::string(4097, 'a')}
  };
  for (const auto& item : cases) {
    context = item.label;
    rejected(read_asset(fixture.root, item.path), "invalid_asset_path");
  }
  for (unsigned control = 0; control <= 0x9f; ++control) {
    if (control >= 0x20 && control < 0x7f) continue;
    context = "control character " + std::to_string(control);
    std::string path = "hello";
    if (control >= 0x80) path += '\xc2';
    path += static_cast<char>(control);
    path += ".txt";
    rejected(read_asset(fixture.root, path), "invalid_asset_path");
  }
  std::string multibyte;
  for (unsigned index = 0; index != 2049; ++index) multibyte += u8"é";
  context = "path cap counts UTF-8 bytes, not characters";
  rejected(read_asset(fixture.root, multibyte), "invalid_asset_path");

  context = "native absolute path";
  rejected(read_asset(fixture.root, (fixture.base / "outside.txt").generic_u8string()),
           "invalid_asset_path");
}

bool link_fixture(const fs::path& target, const fs::path& link, bool directory) {
  std::error_code error;
  if (directory) fs::create_directory_symlink(target, link, error);
  else fs::create_symlink(target, link, error);
  if (!error) return true;
#ifdef _WIN32
  // Windows may require Developer Mode or an elevated symlink privilege.
  // Report only that unavailable branch, not a passing security assertion.
  if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
      (error.category() == std::system_category() && error.value() == 1314)) {
    ++skipped_links;
    std::cout << "NOT EXECUTED [" << context << "]: Windows symlink privilege: "
              << error.message() << '\n';
    return false;
  }
#endif
  CHECK(!error);
  std::cerr << "Cannot create symlink fixture [" << context << "]: " << error.message() << '\n';
  return false;
}

void filesystem_boundaries(files& fixture) {
  context = "missing file";
  rejected(read_asset(fixture.root, "missing.txt"), "asset_not_found");
  context = "directory is not an asset";
  rejected(read_asset(fixture.root, "nested"), "asset_not_found");
  context = "empty resource root";
  rejected(read_asset({}, "hello.txt"), "invalid_root");
  context = "missing resource root";
  rejected(read_asset(fixture.base / "missing-root", "hello.txt"), "invalid_root");
  context = "file resource root";
  rejected(read_asset(fixture.root / "hello.txt", "hello.txt"), "invalid_root");

  context = "in-root file symlink";
  if (link_fixture(fixture.root / "hello.txt", fixture.root / "inside-link.txt", false))
    success(read_asset(fixture.root, "inside-link.txt"), "text/plain", 6, "aGVsbG8K");
  context = "in-root directory symlink";
  if (link_fixture(fixture.root / "nested", fixture.root / "inside-directory", true))
    success(read_asset(fixture.root, "inside-directory/hello.txt"), "text/plain", 6, "aW5zaWRl");
  context = "outside file symlink";
  if (link_fixture(fixture.base / "outside.txt", fixture.root / "outside-link.txt", false))
    rejected(read_asset(fixture.root, "outside-link.txt"), "invalid_asset_path");
  context = "outside directory symlink";
  if (link_fixture(fixture.base, fixture.root / "outside-directory", true))
    rejected(read_asset(fixture.root, "outside-directory/outside.txt"), "invalid_asset_path");
  context = "same-prefix sibling directory is outside root";
  if (link_fixture(fixture.sibling, fixture.root / "prefix-directory", true))
    rejected(read_asset(fixture.root, "prefix-directory/secret.txt"), "invalid_asset_path");
  context = "dangling symlink";
  if (link_fixture(fixture.base / "missing.txt", fixture.root / "dangling.txt", false))
    rejected(read_asset(fixture.root, "dangling.txt"), "asset_not_found");
  context = "symlink back to the root itself";
  if (link_fixture(fixture.root, fixture.root / "root-directory", true))
    rejected(read_asset(fixture.root, "root-directory"), "invalid_asset_path");

#if defined(__unix__) || defined(__APPLE__)
  context = "FIFO is rejected without opening or waiting for a writer";
  const auto fifo = fixture.root / "named-pipe";
  const auto created = ::mkfifo(fifo.c_str(), 0600);
  CHECK(created == 0);
  if (created == 0) rejected(read_asset(fixture.root, "named-pipe"), "asset_not_found");
#endif

  // A rejected request must not poison later reads or leak prior payloads.
  context = "valid read after failures";
  success(read_asset(fixture.root, "hello.txt"), "text/plain", 6, "aGVsbG8K");
}
}  // namespace

int main() {
  try {
    files fixture;
    bytes_and_mime(fixture);
    size_boundaries(fixture);
    invalid_paths(fixture);
    filesystem_boundaries(fixture);
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "Unexpected asset test exception [" << context << "]: " << error.what() << '\n';
  }
  std::cout << "Desktop production filesystem asset reader: " << checks << " checks, "
            << failures << " failures, " << skipped_links << " symlink branches not executed. "
            << "No native GUI/system WebView verification.\n";
  return failures ? 1 : 0;
}
