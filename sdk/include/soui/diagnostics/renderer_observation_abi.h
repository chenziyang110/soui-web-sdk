#ifndef SOUI_WKE_RENDERER_DIAGNOSTICS_H
#define SOUI_WKE_RENDERER_DIAGNOSTICS_H

#include <stdint.h>

#if defined(_WIN32)
#define WKE_RENDER_DIAGNOSTICS_CALL __cdecl
#else
#define WKE_RENDER_DIAGNOSTICS_CALL
#endif

#define WKE_RENDER_DIAGNOSTICS_ABI_VERSION 1U
#define WKE_RENDER_DIAGNOSTICS_CAP_PROCESS_CACHE 0x1ULL
#define WKE_RENDER_DIAGNOSTICS_CAP_COMPLETED_DRAW_CALLS 0x2ULL

#define WKE_RENDER_DIAGNOSTICS_NOT_STARTED 0U
#define WKE_RENDER_DIAGNOSTICS_READY 1U
#define WKE_RENDER_DIAGNOSTICS_BUSY 2U
#define WKE_RENDER_DIAGNOSTICS_UNAVAILABLE 3U
#define WKE_RENDER_DIAGNOSTICS_CLOSED 4U
#define WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT 5U

#define WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID 0x1U
#define WKE_RENDER_DIAGNOSTICS_CLOCK_VALID 0x2U
#define WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED 0x4U
#define WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED 0x8U
#define WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT 0x10U
#define WKE_RENDER_DIAGNOSTICS_OBSERVED_RASTER 0x20U
#define WKE_RENDER_DIAGNOSTICS_OBSERVED_GPU_SKIA 0x40U
#define WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH 0x80U
#define WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE 0x100U

#define WKE_RENDER_DIAGNOSTICS_DRAW_UNKNOWN 0U
#define WKE_RENDER_DIAGNOSTICS_DRAW_BITMAP_RASTER 1U
#define WKE_RENDER_DIAGNOSTICS_DRAW_GPU_SKIA 2U

// All uint64_t fields must be serialized as canonical decimal strings by SDKs.
// Counts are completed synchronous canvas draw calls, not dirty-frame presents,
// GPU fence completion, whole-process graphics coverage or software qualification.
// Lifetime is this DLL instance, not all GPU activity in the OS process.
// No reset exists while loaded; unloading/reloading can create a new epoch.
#pragma pack(push, 8)
typedef struct _wkeRendererProcessSnapshot {
    uint32_t structSize;
    uint32_t abiVersion;
    uint32_t status;
    uint32_t flags;
    uint32_t lastDrawKind;
    uint32_t reserved;
    uint64_t processId;
    uint64_t processCreationFileTime;
    uint64_t engineOwnerThreadId;
    uint64_t qpcFrequency;
    uint64_t recordingEpoch;
    uint64_t renderersCreatedTotal;
    uint64_t renderersClosedTotal;
    uint64_t renderersLive;
    uint64_t rasterDrawsTotal;
    uint64_t gpuSkiaDrawsTotal;
    uint64_t drawEventsTotal;
    uint64_t lastDrawQpc;
    uint64_t lastDrawThreadId;
    uint64_t lastDrawProcessId;
    uint64_t lastRendererCloseQpc;
    uint64_t closedSnapshotQpc;
    uint64_t engineFinalizedQpc;
    uint64_t initializedQpc;
} wkeRendererProcessSnapshot;

typedef struct _wkeRendererDiagnosticsApi {
    uint32_t structSize;
    uint32_t abiVersion;
    uint64_t capabilities;
    // Any caller thread, while the DLL remains loaded. Exactly sizeof(output)
    // is required in output->structSize. Busy/not_started are not valid samples.
    // Getter never accesses a WebView, endpoint, canvas or mutable WKE lifecycle.
    uint32_t (WKE_RENDER_DIAGNOSTICS_CALL* readProcessSnapshot)(
        wkeRendererProcessSnapshot* output);
} wkeRendererDiagnosticsApi;
#pragma pack(pop)

#endif // SOUI_WKE_RENDERER_DIAGNOSTICS_H
