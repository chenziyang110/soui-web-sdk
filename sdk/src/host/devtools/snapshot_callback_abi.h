#pragma once
// Ported from experiments/devtools-stage0/miniblink/snapshot_callback_abi.h.
// Private C-shaped boundary matching the MiniBlink kernel ABI revision 2.
// Bytes remain immutable until final release. Callbacks are noexcept,
// memory-only and nonblocking; no C++ runtime object crosses the DLL.

#include <type_traits>

namespace soui::sdk::detail {
#pragma pack(push, 8)

struct Stage0SnapshotCallbacks {
    unsigned int structSize, abiVersion;
    void* owner;
    void (__cdecl* retain)(void*);
    void (__cdecl* release)(void*);
    unsigned int (__cdecl* lookup)(void*, const char*, unsigned int,
        const unsigned char**, unsigned int*, unsigned int*);
};
struct Stage0SnapshotConfig {
    unsigned int structSize, abiVersion, fileCount, totalBytes;
    char payloadId[65];
    Stage0SnapshotCallbacks callbacks;
};
struct Stage0SnapshotReceipt {
    unsigned int structSize, abiVersion;
    char virtualBase[160];
};
#pragma pack(pop)

static_assert(sizeof(unsigned int) == 4, "Snapshot ABI requires 32-bit unsigned int");
static_assert(sizeof(Stage0SnapshotCallbacks) == (sizeof(void*) == 8 ? 40 : 24));
static_assert(sizeof(Stage0SnapshotConfig) == (sizeof(void*) == 8 ? 128 : 108));
static_assert(sizeof(Stage0SnapshotReceipt) == 168);
static_assert(std::is_standard_layout_v<Stage0SnapshotCallbacks>
    && std::is_trivially_copyable_v<Stage0SnapshotCallbacks>);
static_assert(std::is_standard_layout_v<Stage0SnapshotConfig>
    && std::is_trivially_copyable_v<Stage0SnapshotConfig>);
static_assert(std::is_standard_layout_v<Stage0SnapshotReceipt>
    && std::is_trivially_copyable_v<Stage0SnapshotReceipt>);

} // namespace soui::sdk::detail
