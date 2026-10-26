#ifndef WKE_INPUT_UI_API_H_
#define WKE_INPUT_UI_API_H_

#include <stddef.h>

#if defined(_WIN32)
#define WKE_INPUT_UI_CALL __cdecl
#else
#define WKE_INPUT_UI_CALL
#endif

#define WKE_INPUT_UI_ABI_VERSION 1U
#define WKE_INPUT_UI_CAP_TRACE 0x1ULL
#define WKE_INPUT_UI_CAP_OWNER_OPPORTUNITY 0x2ULL
#define WKE_INPUT_UI_CAP_MIRROR_CONSUMER 0x4ULL
#define WKE_INPUT_UI_CAP_INPUT_CONTEXT 0x8ULL
#define WKE_INPUT_UI_CAP_FRACTIONAL_FRAME_POLICY 0x10ULL
#define WKE_INPUT_UI_CAP_CANCEL_INPUT 0x20ULL
#define WKE_INPUT_UI_CAP_VIEW_CONTEXT 0x40ULL
#define WKE_INPUT_UI_CAP_SCENE_INPUT_IDENTITY 0x80ULL
#define WKE_INPUT_UI_CAP_HOST_VISIBILITY 0x100ULL
#define WKE_INPUT_UI_CAP_EXTERNAL_TRACE_PRODUCER 0x200ULL

#define WKE_INPUT_UI_OK 0
#define WKE_INPUT_UI_INVALID 1
#define WKE_INPUT_UI_BUSY 2
#define WKE_INPUT_UI_REJECTED 3
#define WKE_INPUT_UI_UNAVAILABLE 4
#define WKE_INPUT_UI_EXPORT_LIMIT 5

#define WKE_INPUT_UI_TRACE_AVAILABLE 0x1U
#define WKE_INPUT_UI_TRACE_STOPPED 0x2U
#define WKE_INPUT_UI_TRACE_QUIESCENT 0x4U
#define WKE_INPUT_UI_TRACE_SATURATED 0x8U
#define WKE_INPUT_UI_TRACE_OWNER_VIOLATION 0x10U
#define WKE_INPUT_UI_TRACE_INVALID_EVENT 0x20U
#define WKE_INPUT_UI_TRACE_CLOCK_INVALID 0x40U
#define WKE_INPUT_UI_TRACE_LIMIT_REACHED 0x80U

#define WKE_INPUT_UI_EVENT_BYTES 136U
#define WKE_INPUT_UI_MAX_DRAIN_EVENTS 256U
#define WKE_INPUT_UI_MAX_EXPORT_EVENTS 100000ULL
#define WKE_INPUT_UI_STAGE_CAPACITY 32U
#define WKE_INPUT_UI_COUNTER_CAPACITY 32U
#define WKE_INPUT_UI_DURATION_CAPACITY 16U
#define WKE_INPUT_UI_DURATION_BUCKETS 64U

#pragma pack(push, 8)
typedef struct _wkeInputUiTraceConfig {
    unsigned int structSize;
    unsigned int abiVersion;
    unsigned int mode; /* 0 scalar, 1 detailed; explicit cold configuration. */
    unsigned int processId;
    unsigned long long runIdHigh;
    unsigned long long runIdLow;
    unsigned long long processIncarnation;
    unsigned long long qpcFrequency;
} wkeInputUiTraceConfig;

typedef struct _wkeInputUiTraceSnapshot {
    unsigned int structSize;
    unsigned int abiVersion;
    unsigned int mode;
    unsigned int flags;
    unsigned int registeredThreads;
    unsigned int activeProducers;
    unsigned int pendingRegistrations;
    unsigned int processId;
    unsigned long long runIdHigh;
    unsigned long long runIdLow;
    unsigned long long processIncarnation;
    unsigned long long qpcFrequency;
    unsigned long long attempted;
    unsigned long long recorded;
    unsigned long long dropped;
    unsigned long long queued;
    unsigned long long unregisteredWrites;
    unsigned long long registrationRejections;
    unsigned long long causalLost;
    unsigned int stageCount;
    unsigned int counterCount;
    unsigned int durationMetricCount;
    unsigned int durationBucketCount;
    unsigned long long stages[WKE_INPUT_UI_STAGE_CAPACITY];
    unsigned long long counters[WKE_INPUT_UI_COUNTER_CAPACITY];
    unsigned long long durations[WKE_INPUT_UI_DURATION_CAPACITY][WKE_INPUT_UI_DURATION_BUCKETS];
} wkeInputUiTraceSnapshot;

typedef struct _wkeInputUiApi {
    unsigned int structSize;
    unsigned int abiVersion;
    unsigned long long capabilities;
    /* Numeric trace operations are independent of WebView/COM thread affinity.
       configure is cold and single-session; drain has exactly one consumer. */
    int (WKE_INPUT_UI_CALL* traceConfigure)(const wkeInputUiTraceConfig* config);
    void (WKE_INPUT_UI_CALL* traceStop)(void);
    int (WKE_INPUT_UI_CALL* traceSnapshot)(wkeInputUiTraceSnapshot* snapshot);
    int (WKE_INPUT_UI_CALL* traceDrain)(void* output, unsigned int eventBytes,
        unsigned int capacity, unsigned int* count);
    /* Remaining operations are owner-thread only, capability and pointer gated.
       Opaque view values are actual wkeWebView handles, never remote pointers. */
    void (WKE_INPUT_UI_CALL* serviceOwnerOpportunity)(void);
    int (WKE_INPUT_UI_CALL* setUiMirrorConsumerRequired)(void* view, unsigned int required);
    int (WKE_INPUT_UI_CALL* uiMirrorConsumerRequired)(void* view, unsigned int* required);
    /* Incoming event v1 has producer_slot==0 at byte28. Drained copies carry
       a lifetime slot1..32. Range0 clears/restores a nested input context. */
    int (WKE_INPUT_UI_CALL* setInputTraceContext)(void* view, const void* event, unsigned int eventBytes);
    int (WKE_INPUT_UI_CALL* cancelInputState)(void* view);
    /* Confirmed document/geometry identity, input range zero. Does not push TLS. */
    int (WKE_INPUT_UI_CALL* setViewTraceIdentity)(void* view, const void* event, unsigned int eventBytes);
    int (WKE_INPUT_UI_CALL* setHostVisible)(void* view, unsigned int visible);
    /* Cold, calling-thread-affine producer leases; no implicit hot attach.
       The caller retains the actual module until its final release returns. */
    int (WKE_INPUT_UI_CALL* retainTraceProducer)(void);
    void (WKE_INPUT_UI_CALL* releaseTraceProducer)(void);
} wkeInputUiApi;
#pragma pack(pop)

typedef const wkeInputUiApi* (WKE_INPUT_UI_CALL* FN_wkeGetInputUiApi)(unsigned int requestedVersion);

#ifdef __cplusplus
static_assert(sizeof(wkeInputUiTraceConfig) == 48, "Input trace config ABI changed");
static_assert(offsetof(wkeInputUiApi, traceConfigure) == 16, "Input UI API prefix changed");
static_assert(sizeof(wkeInputUiTraceSnapshot) < 16384, "Input trace snapshot must stay bounded");
#endif

#endif
