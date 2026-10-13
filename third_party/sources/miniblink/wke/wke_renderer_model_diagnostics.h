#ifndef SOUI_WKE_RENDERER_MODEL_DIAGNOSTICS_H
#define SOUI_WKE_RENDERER_MODEL_DIAGNOSTICS_H

#include "wke_renderer_diagnostics.h"

#define WKE_RENDER_MODEL_ABI_VERSION 2U
#define WKE_RENDER_MODEL_CAP_BOUND_BACKEND 0x1ULL
#define WKE_RENDER_MODEL_CAP_FACTORY_HISTORY 0x2ULL
#define WKE_RENDER_MODEL_CAP_COHERENT_COMPOSITOR 0x4ULL
#define WKE_RENDER_MODEL_PROVIDER_COMPATIBILITY 1U
#define WKE_RENDER_MODEL_BACKEND_COMPATIBILITY_COMPOSITOR 1U
#define WKE_RENDER_MODEL_GPU_UNKNOWN 0xffffffffU
#define WKE_RENDER_MODEL_PROVIDER_OBSERVED 0x1U
#define WKE_RENDER_MODEL_IDENTITY_MATCHED 0x2U
#define WKE_RENDER_MODEL_COUNTER_FAULT 0x4U
#define WKE_RENDER_MODEL_CLOCK_FAILURE 0x8U
#define WKE_RENDER_MODEL_BACKEND_OBSERVED 0x10U
#define WKE_RENDER_MODEL_KNOWN_COVERAGE 0x7fU

#define WKE_RENDER_MODEL_FACTORY_MEDIA 1U
#define WKE_RENDER_MODEL_FACTORY_PLUGIN 2U
#define WKE_RENDER_MODEL_FACTORY_OFFSCREEN 3U
#define WKE_RENDER_MODEL_FACTORY_SHARED_CONTEXT 4U
#define WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW 5U
#define WKE_RENDER_MODEL_FACTORY_RTC_STREAM 6U

// Version2 is separate from the unchanged168-byte v1 API. All uint64 fields
// retain canonical decimal strings in consumers. This is one loaded DLL's
// Compatibility workload, not all OS graphics or a qualification certificate.
#pragma pack(push, 8)
typedef struct _wkeRendererModelSnapshot {
    uint32_t structSize;
    uint32_t abiVersion;
    uint32_t status;
    uint32_t flags;
    wkeRendererProcessSnapshot compositor;
    uint32_t providerId;
    uint32_t backendId;
    uint32_t compiledGpuSupport;
    uint32_t factoryCoverageMask;
    uint64_t backendInitializationQpc;
    uint64_t mediaFactoryAttempts;
    uint64_t pluginFactoryAttempts;
    uint64_t offscreenFactoryAttempts;
    uint64_t sharedContextFactoryAttempts;
    uint64_t externalFactoryAttempts;
    uint64_t platformMismatchTotal;
    uint64_t unknownRouteTotal;
} wkeRendererModelSnapshot;

typedef struct _wkeRendererModelDiagnosticsApi {
    uint32_t structSize;
    uint32_t abiVersion;
    uint64_t capabilities;
    uint32_t (WKE_RENDER_DIAGNOSTICS_CALL* readProcessModel)(wkeRendererModelSnapshot* output);
} wkeRendererModelDiagnosticsApi;
#pragma pack(pop)

#endif // SOUI_WKE_RENDERER_MODEL_DIAGNOSTICS_H
