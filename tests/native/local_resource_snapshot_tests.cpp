#include "../../sdk/src/host/local_resource_snapshot.h"

#include <windows.h>
#include <winioctl.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;
using namespace soui::sdk::detail;

namespace soui::sdk::detail {
struct local_resource_snapshot_budget_test final {
    static std::shared_ptr<void> reserve(std::uint64_t bytes) {
        auto credit = std::make_shared<local_resource_snapshot::budget_lease>();
        credit->reserve(bytes);
        return credit;
    }
};
} // namespace soui::sdk::detail

static_assert(!std::is_copy_constructible_v<local_resource_snapshot>
    && !std::is_copy_assignable_v<local_resource_snapshot>
    && !std::is_move_constructible_v<local_resource_snapshot>
    && !std::is_move_assignable_v<local_resource_snapshot>, "Snapshot bodies cannot bypass process credits by copying");

namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

class temporary_tree final {
public:
    temporary_tree() {
        std::array<wchar_t, MAX_PATH + 1> directory{}, name{};
        const auto size = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
        require(size && size < directory.size(), "temporary root path");
        require(GetTempFileNameW(directory.data(), L"slr", 0, name.data()) != 0, "temporary root name");
        require(DeleteFileW(name.data()) != FALSE, "remove temporary name file");
        require(CreateDirectoryW(name.data(), nullptr) != FALSE, "create temporary root");
        root_ = name.data();
    }
    ~temporary_tree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    temporary_tree(const temporary_tree&) = delete;
    temporary_tree& operator=(const temporary_tree&) = delete;
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
    void write(const fs::path& relative, std::string_view bytes) const {
        const auto path = root_ / relative;
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.good(), "open fixture member");
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        require(!stream.fail(), "write fixture member");
    }
    void extent(const fs::path& relative, std::uint64_t bytes) const {
        const auto path = root_ / relative;
        const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "open extent fixture");
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(bytes);
        const bool changed = SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) && SetEndOfFile(handle);
        const bool closed = CloseHandle(handle) != FALSE;
        require(changed && closed, "set extent fixture");
    }
private:
    fs::path root_;
};

template<class Action>
void rejects(local_resource_failure expected, Action&& action) {
    try {
        std::forward<Action>(action)();
    } catch (const local_resource_error& error) {
        require(error.code() == expected, "structured failure kind");
        return;
    }
    throw std::runtime_error("resource preparation unexpectedly succeeded");
}

void immutable_tree_and_empty_member() {
    temporary_tree tree;
    tree.write("index.html", "<script src='assets/app.js'></script>");
    tree.write("assets/app.js", "window.answer=42;");
    tree.write("fixtures/empty.json", "");
    tree.write("fixtures/binary.data", std::string_view("a\0b", 3));
    tree.write(".soui-web-profile.json", "{}");
    const auto snapshot = local_resource_snapshot::prepare(tree.root(), "index.html");
    require(snapshot->file_count() == 5 && snapshot->entry_key() == "index.html", "closed file set");
    const auto* empty = snapshot->lookup("fixtures/empty.json");
    require(empty && empty->bytes.empty() && empty->mime == 4, "empty JSON is present with an exact empty body");
    require(!snapshot->lookup("fixtures/missing.json"), "missing differs from empty");
    const auto* binary = snapshot->lookup("fixtures/binary.data");
    require(binary && binary->bytes == std::vector<std::uint8_t>({'a', 0, 'b'}) && binary->mime == 0,
        "binary body and fallback MIME");
    require(snapshot->lookup("assets/app.js")->mime == 2, "script MIME");
    const auto expected_total = std::string_view("<script src='assets/app.js'></script>").size()
        + std::string_view("window.answer=42;").size() + 3 + 2;
    require(snapshot->total_bytes() == expected_total && snapshot->identity().sha256.size() == 64
        && snapshot->identity().root_file_index, "captured byte count and identity");
    tree.write("assets/app.js", "changed");
    fs::remove_all(tree.root() / "fixtures");
    const auto* original = snapshot->lookup("assets/app.js");
    require(std::string(original->bytes.begin(), original->bytes.end()) == "window.answer=42;"
        && snapshot->lookup("fixtures/empty.json"), "lookup does not revisit the disk");
    require(!snapshot->lookup("ASSETS/app.js"), "lookup cannot introduce a case alias");
}

void stable_identity_and_entry_selection() {
    temporary_tree first, second;
    for (const auto* tree : {&first, &second}) {
        tree->write("index.html", "index");
        tree->write("react.html", "react");
        tree->write("assets/style.CSS", "a{}");
    }
    const auto a = local_resource_snapshot::prepare(first.root(), "");
    const auto b = local_resource_snapshot::prepare(second.root(), "");
    require(a->entry_key() == "react.html" && a->identity().sha256 == b->identity().sha256,
        "default React selection and relocation-stable body identity");
    require(a->lookup("assets/style.CSS")->mime == 3, "case-independent extension MIME");
    const auto explicit_index = local_resource_snapshot::prepare(first.root(), "index.html");
    require(explicit_index->entry_key() == "index.html"
        && explicit_index->identity().sha256 != a->identity().sha256, "entry participates in identity");
    second.write("react.html", "changed");
    const auto changed = local_resource_snapshot::prepare(second.root(), "");
    require(changed->identity().sha256 != b->identity().sha256, "body changes change identity");
    rejects(local_resource_failure::invalid_path, [&] {
        (void)local_resource_snapshot::prepare(first.root(), "./index.html");
    });
    temporary_tree missing;
    missing.write("assets/data.json", "{}");
    rejects(local_resource_failure::missing_entry, [&] {
        (void)local_resource_snapshot::prepare(missing.root(), "");
    });
}

void rejects_url_aliases() {
    constexpr std::string_view invalid[] = {
        "", "/index.html", "./index.html", "../index.html", "a/../index.html", "a//b", "a/b/",
        "a\\b", "a:b", "index.html?x", "index.html#x", "%69ndex.html", "%2e%2e/a", "a/%2F/b",
        "%252e%252e/a", "%00", "a ", "a.", "NUL", "con.txt", "COM1.cfg", "LpT9", "a/b .txt"
    };
    for (const auto key : invalid)
        require(!local_resource_snapshot::valid_key(key), "untrusted URL key rejected");
    require(!local_resource_snapshot::valid_key(std::string_view("a\0b", 3)), "embedded null rejected");
    require(!local_resource_snapshot::valid_key(std::string(129, 'a')), "segment bound");
    require(!local_resource_snapshot::valid_key(std::string(1025, 'a')), "key bound");
    std::string depth;
    for (unsigned i = 1; i < 32; ++i) depth += "a/";
    require(local_resource_snapshot::valid_key(depth + "f"), "depth boundary supported");
    require(!local_resource_snapshot::valid_key(depth + "a/f"), "depth overflow rejected");
    require(local_resource_snapshot::valid_key(".soui-web-profile.json")
        && local_resource_snapshot::valid_key("assets/fonts/Font-Name_1.woff2"), "ordinary captured keys supported");
    temporary_tree tree;
    tree.write("index.html", "");
    const auto snapshot = local_resource_snapshot::prepare(tree.root(), "");
    for (const auto key : invalid) require(!snapshot->lookup(key), "memory lookup cannot accept URL aliases");
    rejects(local_resource_failure::invalid_root, [&] {
        (void)local_resource_snapshot::prepare(fs::path("relative"), "");
    });
    rejects(local_resource_failure::invalid_root, [&] {
        (void)local_resource_snapshot::prepare(fs::path(L"\\\\server\\share"), "");
    });
    rejects(local_resource_failure::invalid_root, [&] {
        (void)local_resource_snapshot::prepare(tree.root() / L".", "");
    });
}

void rejects_hardlinks_and_existing_writer() {
    temporary_tree tree, outside;
    tree.write("index.html", "index");
    outside.write("private.txt", "not an application resource");
    require(CreateHardLinkW((tree.root() / "linked.txt").c_str(),
        (outside.root() / "private.txt").c_str(), nullptr) != FALSE, "create hardlink fixture");
    rejects(local_resource_failure::identity, [&] {
        (void)local_resource_snapshot::prepare(tree.root(), "");
    });
    require(DeleteFileW((tree.root() / "linked.txt").c_str()) != FALSE, "remove hardlink fixture");
    const auto writer = CreateFileW((tree.root() / "index.html").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    require(writer != INVALID_HANDLE_VALUE, "open conflicting writer");
    bool refused{};
    try {
        (void)local_resource_snapshot::prepare(tree.root(), "");
    } catch (const local_resource_error& error) {
        refused = error.code() == local_resource_failure::open
            && error.native_error() == ERROR_SHARING_VIOLATION;
    } catch (...) {
        CloseHandle(writer);
        throw;
    }
    const bool closed = CloseHandle(writer) != FALSE;
    require(refused && closed, "capture refuses an already writable source");
    require(local_resource_snapshot::prepare(tree.root(), "")->file_count() == 1,
        "conflicting writer failure does not poison a later capture");
}

class directory_junction final {
public:
    directory_junction(fs::path link, const fs::path& target) : link_(std::move(link)) {
        require(CreateDirectoryW(link_.c_str(), nullptr) != FALSE, "create junction directory");
        const auto handle = CreateFileW(link_.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            RemoveDirectoryW(link_.c_str());
            throw std::runtime_error("open junction fixture");
        }
        const auto substitute = L"\\??\\" + target.native();
        const auto print = target.native();
        struct junction_data final {
            DWORD tag;
            WORD data_length, reserved;
            WORD substitute_offset, substitute_length, print_offset, print_length;
            wchar_t path[4096];
        } data{};
        data.tag = IO_REPARSE_TAG_MOUNT_POINT;
        data.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
        data.print_offset = static_cast<WORD>(data.substitute_length + sizeof(wchar_t));
        data.print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
        const auto path_bytes = static_cast<WORD>(data.print_offset + data.print_length + sizeof(wchar_t));
        data.data_length = static_cast<WORD>(8 + path_bytes);
        const bool fits = substitute.size() + print.size() + 2 <= std::size(data.path);
        if (fits) {
            std::memcpy(data.path, substitute.c_str(), data.substitute_length + sizeof(wchar_t));
            std::memcpy(reinterpret_cast<unsigned char*>(data.path) + data.print_offset,
                print.c_str(), data.print_length + sizeof(wchar_t));
        }
        DWORD returned{};
        const bool created = fits && DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
            static_cast<DWORD>(8 + data.data_length), nullptr, 0, &returned, nullptr) != FALSE;
        const bool closed = CloseHandle(handle) != FALSE;
        if (!created || !closed) {
            RemoveDirectoryW(link_.c_str());
            throw std::runtime_error("create junction fixture");
        }
    }
    ~directory_junction() { RemoveDirectoryW(link_.c_str()); }
    directory_junction(const directory_junction&) = delete;
    directory_junction& operator=(const directory_junction&) = delete;
    [[nodiscard]] const fs::path& path() const noexcept { return link_; }
private:
    fs::path link_;
};

void rejects_reparse_member_and_ancestor() {
    temporary_tree tree, outside;
    tree.write("index.html", "index");
    outside.write("resources/index.html", "private");
    const directory_junction junction(tree.root() / "escape", outside.root());
    rejects(local_resource_failure::reparse, [&] {
        (void)local_resource_snapshot::prepare(tree.root(), "");
    });
    rejects(local_resource_failure::reparse, [&] {
        (void)local_resource_snapshot::prepare(junction.path() / "resources", "");
    });
    require(fs::is_regular_file(outside.root() / "resources/index.html"), "outside member remains intact");
}

void rejects_budgets_before_body_allocation() {
    {
        temporary_tree tree;
        tree.write("index.html", "");
        tree.extent("oversized.bin", local_resource_file_bytes + 1);
        rejects(local_resource_failure::budget, [&] {
            (void)local_resource_snapshot::prepare(tree.root(), "");
        });
    }
    {
        temporary_tree tree;
        tree.write("index.html", "");
        for (unsigned i = 0; i < 9; ++i)
            tree.extent(std::to_string(i) + ".bin", local_resource_file_bytes);
        rejects(local_resource_failure::budget, [&] {
            (void)local_resource_snapshot::prepare(tree.root(), "");
        });
    }
    {
        temporary_tree tree;
        tree.write("index.html", "");
        for (unsigned i = 0; i < local_resource_files; ++i)
            tree.write(std::to_string(i) + ".txt", "");
        rejects(local_resource_failure::budget, [&] {
            (void)local_resource_snapshot::prepare(tree.root(), "");
        });
    }
}

void process_snapshot_capacity_and_shared_ownership() {
    temporary_tree tree;
    tree.write("index.html", "fixed");
    std::vector<std::shared_ptr<const local_resource_snapshot>> snapshots;
    for (unsigned i = 0; i < 32; ++i)
        snapshots.push_back(local_resource_snapshot::prepare(tree.root(), ""));
    rejects(local_resource_failure::budget, [&] {
        (void)local_resource_snapshot::prepare(tree.root(), "");
    });
    auto shared_last = snapshots.back();
    snapshots.pop_back();
    rejects(local_resource_failure::budget, [&] {
        (void)local_resource_snapshot::prepare(tree.root(), "");
    });
    require(shared_last->lookup("index.html"), "A shared body retains its one original process credit");
    shared_last.reset();
    auto replacement = local_resource_snapshot::prepare(tree.root(), "");
    require(replacement->file_count() == 1, "Actual last-owner destruction restores one process credit");
    snapshots.clear();
    replacement.reset();

    for (unsigned i = 0; i < 40; ++i) {
        rejects(local_resource_failure::invalid_path, [&] {
            (void)local_resource_snapshot::prepare(tree.root(), "../index.html");
        });
    }
    for (unsigned i = 0; i < 32; ++i)
        snapshots.push_back(local_resource_snapshot::prepare(tree.root(), ""));
    require(snapshots.size() == 32, "Failed preparation returns every preparation credit");
}

void process_byte_capacity_and_failed_capture_cleanup() {
    temporary_tree valid, missing;
    valid.write("index.html", "fixed");
    missing.write("data.json", "body");
    for (unsigned i = 0; i < 40; ++i) {
        rejects(local_resource_failure::missing_entry, [&] {
            (void)local_resource_snapshot::prepare(missing.root(), "");
        });
    }

    // These are real production byte credits, not allocated file-body memory.
    // Exact 512 MiB admission after failed real captures also proves their
    // charged bytes were returned. This is not a 512 MiB end-to-end memory run.
    std::array<std::shared_ptr<void>, 4> credits;
    for (auto& credit : credits)
        credit = local_resource_snapshot_budget_test::reserve(128ULL * 1024 * 1024);
    rejects(local_resource_failure::budget, [] {
        (void)local_resource_snapshot_budget_test::reserve(1);
    });
    for (unsigned i = 0; i < 40; ++i) {
        rejects(local_resource_failure::budget, [&] {
            (void)local_resource_snapshot::prepare(valid.root(), "");
        });
    }
    auto retained_credit = credits.front();
    credits.front().reset();
    rejects(local_resource_failure::budget, [&] {
        (void)local_resource_snapshot::prepare(valid.root(), "");
    });
    retained_credit.reset();
    const auto recovered = local_resource_snapshot::prepare(valid.root(), "");
    require(recovered->lookup("index.html"), "Actual final-credit release permits a new bounded capture");
    // The following actual 32-snapshot case checks that all failed reservations
    // above returned their preparation credits as well as their byte credits.
}
} // namespace

int main() {
    try {
        immutable_tree_and_empty_member();
        stable_identity_and_entry_selection();
        rejects_url_aliases();
        rejects_hardlinks_and_existing_writer();
        rejects_reparse_member_and_ancestor();
        rejects_budgets_before_body_allocation();
        process_byte_capacity_and_failed_capture_cleanup();
        process_snapshot_capacity_and_shared_ownership();
        std::cout << "local resource snapshot behavior passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "local resource snapshot behavior failed: " << error.what() << '\n';
        return 1;
    }
}
