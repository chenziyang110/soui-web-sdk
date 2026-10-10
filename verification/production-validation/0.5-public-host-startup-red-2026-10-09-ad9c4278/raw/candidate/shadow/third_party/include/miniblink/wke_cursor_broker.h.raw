#ifndef WKE_CURSOR_BROKER_H
#define WKE_CURSOR_BROKER_H

/* Included by wke.h / wkedefine.h after wkeWebView. Fixed C ABI, pack 8. */
#define WKE_CURSOR_BROKER_ABI_VERSION 1U
#define WKE_CURSOR_BROKER_MAX_ENDPOINTS 64U
#define WKE_CURSOR_BROKER_MAX_NATIVE_TASKS 64U
#define WKE_CURSOR_BROKER_MAX_SIDE 256U
#define WKE_CURSOR_BROKER_CAP_NPAPI_UNAVAILABLE 1U
#define WKE_CURSOR_BROKER_CAP_DESCRIPTOR_ONLY 2U

#pragma pack(push, 8)
typedef unsigned __int64 wkeCursorBrokerSequence;
typedef struct _wkeCursorBrokerToken {
    unsigned __int64 low;
    unsigned __int64 high;
} wkeCursorBrokerToken;
typedef wkeCursorBrokerToken wkeCursorBrokerRealm;
typedef wkeCursorBrokerToken wkeCursorBrokerEndpoint;

typedef enum _wkeCursorBrokerStatus {
    WKE_CURSOR_BROKER_OK = 0,
    WKE_CURSOR_BROKER_INVALID = 1,
    WKE_CURSOR_BROKER_WRONG_THREAD = 2,
    WKE_CURSOR_BROKER_UNAVAILABLE = 3,
    WKE_CURSOR_BROKER_HISTORY = 4,
    WKE_CURSOR_BROKER_STALE = 5,
    WKE_CURSOR_BROKER_CLOSING = 6,
    WKE_CURSOR_BROKER_BUSY = 7,
    WKE_CURSOR_BROKER_LIMIT = 8,
    WKE_CURSOR_BROKER_FAILED = 9
} wkeCursorBrokerStatus;

typedef enum _wkeCursorBrokerKind {
    WKE_CURSOR_BROKER_STANDARD = 0,
    WKE_CURSOR_BROKER_NONE = 1,
    WKE_CURSOR_BROKER_CUSTOM_BGRA8 = 2,
    WKE_CURSOR_BROKER_UNSUPPORTED = 3
} wkeCursorBrokerKind;
typedef enum _wkeCursorBrokerNativeKind {
    WKE_CURSOR_BROKER_NATIVE_TASK = 0,
    WKE_CURSOR_BROKER_NATIVE_MENU = 1,
    WKE_CURSOR_BROKER_NATIVE_OLE_DRAG = 2,
    WKE_CURSOR_BROKER_NATIVE_OLE_REGISTER = 3,
    WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE = 4
} wkeCursorBrokerNativeKind;

typedef struct _wkeCursorBrokerIdentity {
    unsigned int structSize;
    unsigned int reserved;
    unsigned __int64 viewGeneration;
    unsigned __int64 documentGeneration;
    unsigned __int64 geometryGeneration;
} wkeCursorBrokerIdentity;

typedef struct _wkeCursorBrokerDescriptor {
    unsigned int structSize;
    unsigned int kind;
    wkeCursorBrokerIdentity identity;
    wkeCursorBrokerSequence publicationSequence;
    unsigned int cursorType; /* Blink WebCursorInfo::Type; not a native handle. */
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    int hotspotX;
    int hotspotY;
    unsigned int pixelBytes;
    unsigned int reserved; /* BGRA8 premultiplied, top-down, stride = width * 4. */
    const unsigned char* pixels; /* Borrowed only until onDescriptor returns. */
} wkeCursorBrokerDescriptor;

typedef struct _wkeCursorBrokerNativeReceipt {
    unsigned int structSize;
    unsigned int kind;
    wkeCursorBrokerEndpoint endpoint;
    wkeCursorBrokerIdentity identity;
    wkeCursorBrokerSequence taskSequence;
    unsigned __int64 scopeSequence;
} wkeCursorBrokerNativeReceipt;

/* onDescriptor runs on the engine owner and must copy into a bounded latest
 * slot. The native hooks run only on the HWND owner. They must not call WKE,
 * wait for the engine, pump messages, throw, or retain a descriptor pointer.
 * enter returns 1 only after the SDK has pinned its queue-local cursor lease.
 * leave balances successful enter, including exceptions/cancellation.
 * completion runs once after disposal of a queued UI payload. Hook storage and
 * userData stay alive until destroyEndpoint's successful drain receipt. */
typedef void (WKE_CALL_TYPE* wkeCursorBrokerOnDescriptor)(void*, wkeCursorBrokerEndpoint, const wkeCursorBrokerDescriptor*);
typedef int (WKE_CALL_TYPE* wkeCursorBrokerOnNativeEnter)(void*, const wkeCursorBrokerNativeReceipt*);
typedef void (WKE_CALL_TYPE* wkeCursorBrokerOnNativeLeave)(void*, const wkeCursorBrokerNativeReceipt*);
typedef void (WKE_CALL_TYPE* wkeCursorBrokerOnNativeComplete)(void*, const wkeCursorBrokerNativeReceipt*, unsigned int cancelled);
typedef struct _wkeCursorBrokerHooks {
    unsigned int structSize;
    unsigned int reserved;
    void* userData;
    wkeCursorBrokerOnDescriptor onDescriptor;
    wkeCursorBrokerOnNativeEnter onNativeEnter;
    wkeCursorBrokerOnNativeLeave onNativeLeave;
    wkeCursorBrokerOnNativeComplete onNativeComplete;
} wkeCursorBrokerHooks;

typedef struct _wkeCursorBrokerRealmConfig {
    unsigned int structSize;
    unsigned int maxEndpoints;
    unsigned int maxNativeTasks;
    unsigned int reserved;
} wkeCursorBrokerRealmConfig;
typedef struct _wkeCursorBrokerRealmReceipt {
    unsigned int structSize;
    unsigned int capabilities;
    wkeCursorBrokerRealm realm;
    unsigned __int64 realmGeneration;
} wkeCursorBrokerRealmReceipt;
typedef struct _wkeCursorBrokerEndpointConfig {
    unsigned int structSize;
    unsigned int reserved;
    HWND ownerWindow;
    wkeCursorBrokerIdentity identity;
    wkeCursorBrokerHooks hooks;
} wkeCursorBrokerEndpointConfig;
typedef struct _wkeCursorBrokerEndpointReceipt {
    unsigned int structSize;
    unsigned int ownerThreadId;
    wkeCursorBrokerEndpoint endpoint;
    wkeWebView webView; /* Engine-only legacy APIs; never destroy via raw WKE. */
    wkeCursorBrokerIdentity identity;
} wkeCursorBrokerEndpointReceipt;
typedef struct _wkeCursorBrokerDrainReceipt {
    unsigned int structSize;
    unsigned int closing;
    wkeCursorBrokerEndpoint endpoint;
    unsigned int queuedUiTasks;
    unsigned int runningUiTasks;
    unsigned int nativeScopes;
    unsigned int engineCallbacks;
    unsigned int oleRegistered;
    unsigned int drained;
    unsigned __int64 closeSequence;
} wkeCursorBrokerDrainReceipt;

/* Every table operation is engine-owner-only and nonblocking. The UI hooks
 * above are separate plain callbacks, not permissions to invoke this table. */
typedef struct _wkeCursorBrokerApi {
    unsigned int structSize;
    unsigned int abiVersion;
    unsigned int capabilities;
    unsigned int reserved;
    unsigned int (WKE_CALL_TYPE* beginRealm)(const wkeCursorBrokerRealmConfig*, wkeCursorBrokerRealmReceipt*);
    unsigned int (WKE_CALL_TYPE* createEndpoint)(wkeCursorBrokerRealm, const wkeCursorBrokerEndpointConfig*, wkeCursorBrokerEndpointReceipt*);
    unsigned int (WKE_CALL_TYPE* updateIdentity)(wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*);
    unsigned int (WKE_CALL_TYPE* requestClose)(wkeCursorBrokerEndpoint);
    unsigned int (WKE_CALL_TYPE* inspectDrain)(wkeCursorBrokerEndpoint, wkeCursorBrokerDrainReceipt*);
    unsigned int (WKE_CALL_TYPE* destroyEndpoint)(wkeCursorBrokerEndpoint, wkeCursorBrokerDrainReceipt*);
    unsigned int (WKE_CALL_TYPE* endRealm)(wkeCursorBrokerRealm);
} wkeCursorBrokerApi;
#pragma pack(pop)

typedef const wkeCursorBrokerApi* (WKE_CALL_TYPE* FN_wkeGetCursorBrokerApi)(unsigned int requestedVersion);

#endif /* WKE_CURSOR_BROKER_H */
