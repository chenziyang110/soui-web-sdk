#ifndef SOUI_WKE_VIEW_DIAGNOSTICS_H
#define SOUI_WKE_VIEW_DIAGNOSTICS_H

#include <soui/diagnostics/renderer_observation_abi.h>

#define WKE_VIEW_DIAGNOSTICS_ABI_VERSION 1U
#define WKE_VIEW_DIAGNOSTICS_CAP_DLL_LIFETIMES 0x1ULL

// CWebView wrapper lifetimes in this loaded DLL only. Not compositor counts,
// all Blink pages/frames, process children, callback totals or permission.
// Serialize every uint64_t as a canonical decimal string.
#pragma pack(push, 8)
typedef struct _wkeViewLifetimeSnapshot {
    uint32_t structSize;
    uint32_t abiVersion;
    uint32_t status;
    uint32_t flags;
    uint64_t processId;
    uint64_t processCreationFileTime;
    uint64_t engineOwnerThreadId;
    uint64_t qpcFrequency;
    uint64_t recordingEpoch;
    uint64_t mutationSequence;
    uint64_t viewsCreatedTotal;
    uint64_t viewsDestroyedTotal;
    uint64_t viewsLive;
    uint64_t lastMutationQpc;
    uint64_t engineFinalizedQpc;
} wkeViewLifetimeSnapshot;

typedef struct _wkeViewDiagnosticsApi {
    uint32_t structSize;
    uint32_t abiVersion;
    uint64_t capabilities;
    // Any caller while the DLL remains loaded. Exact output size required.
    // A busy result is not a zero sample. Getter only copies a fixed cache.
    uint32_t (WKE_RENDER_DIAGNOSTICS_CALL* readViewLifetimes)(wkeViewLifetimeSnapshot* output);
} wkeViewDiagnosticsApi;
#pragma pack(pop)

#endif
