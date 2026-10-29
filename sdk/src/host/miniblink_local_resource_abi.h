#ifndef SOUI_MINIBLINK_LOCAL_RESOURCE_API_H
#define SOUI_MINIBLINK_LOCAL_RESOURCE_API_H
#include <stdint.h>

// Private optional ABI. Independent from inspector control/resource versions.
#define SOUI_LOCAL_RESOURCE_ABI 1U
#define SOUI_LOCAL_RESOURCE_CALL __cdecl
#define SOUI_LOCAL_RESOURCE_MAX_OPERATIONS 64U
#define SOUI_LOCAL_RESOURCE_MAX_QUEUED_OPERATIONS 64U
#define SOUI_LOCAL_RESOURCE_MAX_CALLBACK_BORROWS 128U
#define SOUI_LOCAL_RESOURCE_MAX_BORROWED_BYTES (128ULL * 1024 * 1024)
enum SouiLocalResourceStatus { SouiLocalOk = 0, SouiLocalPending = 1,
    SouiLocalInvalid = 2, SouiLocalStale = 3, SouiLocalWrongThread = 4,
    SouiLocalFailed = 5, SouiLocalLimit = 6 };
enum SouiLocalResourcePhase { SouiLocalReady = 1, SouiLocalClosing = 2,
    SouiLocalPayloadRetired = 3 };
enum SouiLocalResourceMime { SouiLocalOctet = 0, SouiLocalHtml, SouiLocalJavascript,
    SouiLocalCss, SouiLocalJson, SouiLocalPng, SouiLocalSvg, SouiLocalGif,
    SouiLocalJpeg, SouiLocalWoff, SouiLocalWoff2, SouiLocalPlain, SouiLocalWasm,
    SouiLocalIco, SouiLocalWebp, SouiLocalTtf, SouiLocalOtf, SouiLocalXml,
    SouiLocalPdf, SouiLocalMp4, SouiLocalMp3 };
#pragma pack(push, 8)
struct SouiLocalResourceEndpoint { uint64_t low, high; };
struct SouiLocalResourceCallbacks {
    uint32_t structSize, abiVersion;
    void* owner;
    // All owner callbacks must be nonthrowing; retain/release are exact
    // reference operations, not asynchronous requests.
    void (SOUI_LOCAL_RESOURCE_CALL* retain)(void*);
    void (SOUI_LOCAL_RESOURCE_CALL* release)(void*);
    // 1 = immutable found bytes, 0 = absent (404), other = owner failure.
    // The pointer remains valid until the retained callback owner is released.
    uint32_t (SOUI_LOCAL_RESOURCE_CALL* lookup)(void*, const char*, uint32_t,
        const unsigned char**, uint32_t*, uint32_t*);
};
struct SouiLocalResourceConfig {
    uint32_t structSize, abiVersion, fileCount, totalBytes;
    char hostNonce[33]; // 128 random bits, lowercase hex, stable for one mount.
    char entryRelative[1025];
    char payloadId[65]; // SHA-256 of the SDK's closed immutable snapshot.
    SouiLocalResourceCallbacks callbacks;
};
struct SouiLocalResourceReceipt {
    uint32_t structSize, abiVersion;
    uint64_t viewGeneration, boundBrokerDocumentGeneration, documentReceipt;
    char virtualBase[160];
    char entryUrl[1185];
};
struct SouiLocalResourceDrain {
    uint32_t structSize, abiVersion, phase, failed;
    uint64_t viewGeneration, documentReceipt, closeSequence;
    uint32_t activeOperations, callbackBorrows, queuedOperations, payloadHeld;
    uint64_t borrowedBytes, started, completed, cancelled, denied, deliveredBytes;
};
struct SouiLocalResourceApi {
    uint32_t structSize, abiVersion;
    uint64_t capabilities; // 1 memory replies, 2 native retirement, 4 pause gate.
    uint32_t (SOUI_LOCAL_RESOURCE_CALL* bind)(SouiLocalResourceEndpoint,
        uint64_t expectedViewGeneration, uint64_t expectedBrokerDocumentGeneration,
        const SouiLocalResourceConfig*, SouiLocalResourceReceipt*);
    uint32_t (SOUI_LOCAL_RESOURCE_CALL* beginClose)(SouiLocalResourceEndpoint,
        uint64_t documentReceipt, SouiLocalResourceDrain*);
    uint32_t (SOUI_LOCAL_RESOURCE_CALL* queryDrain)(SouiLocalResourceEndpoint,
        uint64_t documentReceipt, SouiLocalResourceDrain*);
};
#pragma pack(pop)
typedef const SouiLocalResourceApi* (SOUI_LOCAL_RESOURCE_CALL* SouiGetLocalResourceApi)(uint32_t);
#endif
