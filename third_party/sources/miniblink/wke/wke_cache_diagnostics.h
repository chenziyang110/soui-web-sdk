#ifndef SOUI_WKE_CACHE_DIAGNOSTICS_H
#define SOUI_WKE_CACHE_DIAGNOSTICS_H
#include <stdint.h>
#if defined(_WIN32)
#define WKE_CACHE_CALL __cdecl
#else
#define WKE_CACHE_CALL
#endif
#define WKE_CACHE_ABI_VERSION 1U
#define WKE_CACHE_CAP_NAMED_SCALARS 7ULL
#define WKE_CACHE_NOT_STARTED 0U
#define WKE_CACHE_READY 1U
#define WKE_CACHE_BUSY 2U
#define WKE_CACHE_UNAVAILABLE 3U
#define WKE_CACHE_WRONG_OWNER 4U
#define WKE_CACHE_INVALID_ARGUMENT 5U

// Optional and separate from renderer v1/v2. Components are sampled under their
// own locks, not one atomic heap snapshot. Numeric fields are valid only when
// the corresponding status is READY. Serialize uint64_t as decimal strings.
#pragma pack(push, 8)
typedef struct _wkeCacheSnapshot {
    uint32_t structSize, abiVersion, status, reserved;
    uint64_t processId, processCreationFileTime, engineOwnerThreadId;
    uint64_t recordingEpoch, initializedQpc, sampleQpc, qpcFrequency;
    uint32_t blinkStatus, blinkReserved;
    uint64_t blinkLiveBytes, blinkDeadBytes, blinkCapacity, blinkMinDeadCapacity, blinkMaxDeadCapacity;
    uint32_t glyphStatus, glyphReserved;
    uint64_t glyphAttachedBytes, glyphAttachedCount, glyphByteLimit, glyphCountLimit;
    uint32_t resourceStatus, resourceDiscardable;
    uint64_t resourceRetainedBytes, resourceRetainedCount, resourceByteLimit, resourceCountLimit;
    uint64_t resourceRawSingleLimit, resourceEffectiveSingleLimit;
} wkeCacheSnapshot;
typedef struct _wkeCacheDiagnosticsApi {
    uint32_t structSize, abiVersion;
    uint64_t capabilities;
    // Engine-owner only while initialized. Never creates, prunes or traverses.
    uint32_t (WKE_CACHE_CALL* sampleExistingCaches)(wkeCacheSnapshot* output);
} wkeCacheDiagnosticsApi;
#pragma pack(pop)
#endif
