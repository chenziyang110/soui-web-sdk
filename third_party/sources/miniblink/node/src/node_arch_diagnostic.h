#ifndef SOUI_NODE_ARCH_DIAGNOSTIC_H_
#define SOUI_NODE_ARCH_DIAGNOSTIC_H_

#include <stdint.h>

// Diagnostic-only ABI. No Environment, Isolate, Context or V8 handle crosses it.
enum SouiNodeArchStatusV1 {
    SOUI_NODE_ARCH_OBSERVED = 0,
    SOUI_NODE_ARCH_UNOBSERVED = 1,
    SOUI_NODE_ARCH_UNSUPPORTED = 2,
    SOUI_NODE_ARCH_INVALID_PROPERTIES = 3,
    SOUI_NODE_ARCH_BAD_ARGUMENT = 4,
    SOUI_NODE_ARCH_ALREADY_INITIALIZED = 5,
    SOUI_NODE_ARCH_CLEANUP_FAILED = 6
};

enum SouiNodeArchOriginV1 {
    SOUI_NODE_ARCH_ORIGIN_NONE = 0,
    SOUI_NODE_ARCH_ORIGIN_PRODUCTION_CREATE = 1,
    SOUI_NODE_ARCH_ORIGIN_DIAGNOSTIC_FIRST_CREATE = 2
};

#pragma pack(push, 8)
struct SouiNodeArchSnapshotV1 {
    uint32_t structBytes;
    uint32_t abiVersion;
    uint32_t status;
    uint32_t origin;
    uint32_t pointerBytes;
    uint32_t observationSequence;
    uint32_t observerThreadId;
    uint32_t nodeInitObserved;
    uint32_t environmentCreated;
    uint32_t diagnosticLoadEnvironmentCalled;
    uint32_t environmentDisposed;
    uint32_t privateLoopClosed;
    uint32_t privateIsolateDisposed;
    uint32_t retainedUntilProcessExit;
    uint32_t nodeGlobalResourcesUntilProcessExit;
    uint32_t reserved;
    char arch[16];
    char version[64];
    char libUrl[512];
};
#pragma pack(pop)

typedef int (__cdecl* SouiNodeArchitectureReadV1)(SouiNodeArchSnapshotV1*, uint32_t);
typedef int (__cdecl* SouiNodeArchitectureDiagnosticFirstFnV1)(SouiNodeArchSnapshotV1*, uint32_t);

#endif
