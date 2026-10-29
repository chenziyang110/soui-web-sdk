// Included only by node1.cc, inside namespace node, when the explicit
// SOUI_NODE_ARCH_DIAGNOSTIC=1 diagnostic build is selected. Default build is off.
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:SouiNodeArchitectureReadSnapshotV1=_SouiNodeArchitectureReadSnapshotV1")
#pragma comment(linker, "/EXPORT:SouiNodeArchitectureDiagnosticFirstV1=_SouiNodeArchitectureDiagnosticFirstV1")
#endif
static SRWLOCK soui_node_arch_snapshot_lock = SRWLOCK_INIT;
static SouiNodeArchSnapshotV1 soui_node_arch_snapshot = {};
static LONG soui_node_arch_first_attempt = 0;
static thread_local bool soui_node_arch_diagnostic_create = false;

static void SouiNodeArchEmpty(SouiNodeArchSnapshotV1* out, uint32_t status) {
    memset(out, 0, sizeof(*out));
    out->structBytes = static_cast<uint32_t>(sizeof(*out));
    out->abiVersion = 1;
    out->status = status;
    out->pointerBytes = static_cast<uint32_t>(sizeof(void*));
}

static bool SouiNodeArchString(Environment* env, Local<Object> object,
                              const char* key, char* out, size_t capacity) {
    Local<String> name;
    Local<Value> value;
    if (!String::NewFromUtf8(env->isolate(), key, v8::NewStringType::kNormal).ToLocal(&name)
        || !object->Get(env->context(), name).ToLocal(&value) || !value->IsString())
        return false;
    String::Utf8Value text(value);
    if (!*text || text.length() <= 0 || static_cast<size_t>(text.length()) >= capacity)
        return false;
    // These production properties are ASCII. Reject truncation/embedded NUL.
    for (int i = 0; i < text.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>((*text)[i]);
        if (ch < 0x20 || ch > 0x7e)
            return false;
    }
    memcpy(out, *text, static_cast<size_t>(text.length()));
    out[text.length()] = '\0';
    return true;
}

static void SouiNodeArchCapture(Environment* env) {
    SouiNodeArchSnapshotV1 next;
    SouiNodeArchEmpty(&next, SOUI_NODE_ARCH_INVALID_PROPERTIES);
    next.origin = soui_node_arch_diagnostic_create
        ? SOUI_NODE_ARCH_ORIGIN_DIAGNOSTIC_FIRST_CREATE
        : SOUI_NODE_ARCH_ORIGIN_PRODUCTION_CREATE;
    next.observerThreadId = GetCurrentThreadId();
    next.nodeInitObserved = node_is_initialized ? 1u : 0u;
    next.environmentCreated = 1;
    TryCatch try_catch(env->isolate());
    Local<Object> process = env->process_object();
    Local<String> release_name;
    Local<Value> release;
    if (!process.IsEmpty() && next.nodeInitObserved
        && SouiNodeArchString(env, process, "arch", next.arch, sizeof(next.arch))
        && SouiNodeArchString(env, process, "version", next.version, sizeof(next.version))
        && String::NewFromUtf8(env->isolate(), "release", v8::NewStringType::kNormal).ToLocal(&release_name)
        && process->Get(env->context(), release_name).ToLocal(&release)
        && release->IsObject()
        && SouiNodeArchString(env, release.As<Object>(), "libUrl", next.libUrl, sizeof(next.libUrl)))
        next.status = SOUI_NODE_ARCH_OBSERVED;
    else if (!next.nodeInitObserved)
        next.status = SOUI_NODE_ARCH_UNOBSERVED;
    // A diagnostic read failure must not become a production exception.
    if (try_catch.HasCaught())
        try_catch.Reset();
    AcquireSRWLockExclusive(&soui_node_arch_snapshot_lock);
    next.observationSequence = soui_node_arch_snapshot.observationSequence + 1;
    soui_node_arch_snapshot = next;
    ReleaseSRWLockExclusive(&soui_node_arch_snapshot_lock);
}

extern "C" __declspec(dllexport) int __cdecl SouiNodeArchitectureReadSnapshotV1(
    SouiNodeArchSnapshotV1* out, uint32_t bytes) {
    if (!out || bytes != sizeof(*out))
        return SOUI_NODE_ARCH_BAD_ARGUMENT;
    AcquireSRWLockShared(&soui_node_arch_snapshot_lock);
    *out = soui_node_arch_snapshot;
    ReleaseSRWLockShared(&soui_node_arch_snapshot_lock);
    if (!out->observationSequence)
        SouiNodeArchEmpty(out, SOUI_NODE_ARCH_UNOBSERVED);
    return static_cast<int>(out->status);
}

struct SouiNodeArchLoopMembers {
    uv_handle_t* handles[5];
    unsigned seen;
    bool unexpected;
};

static void SouiNodeArchCheckLoop(uv_handle_t* handle, void* arg) {
    SouiNodeArchLoopMembers* members = static_cast<SouiNodeArchLoopMembers*>(arg);
    for (unsigned i = 0; i < 5; ++i) {
        if (members->handles[i] == handle) {
            const unsigned bit = 1u << i;
            if ((members->seen & bit) || uv_is_closing(handle))
                members->unexpected = true;
            members->seen |= bit;
            return;
        }
    }
    members->unexpected = true;
}

// One fresh exclusive process only. This is an acceptance fixture, not an
// implementation of production Node hosting, restart, JS bootstrap or shutdown.
extern "C" __declspec(dllexport) int __cdecl SouiNodeArchitectureDiagnosticFirstV1(
    SouiNodeArchSnapshotV1* out, uint32_t bytes) {
    if (!out || bytes != sizeof(*out))
        return SOUI_NODE_ARCH_BAD_ARGUMENT;
    SouiNodeArchEmpty(out, SOUI_NODE_ARCH_UNSUPPORTED);
    if (InterlockedCompareExchange(&soui_node_arch_first_attempt, 1, 0) != 0) {
        out->status = SOUI_NODE_ARCH_ALREADY_INITIALIZED;
        return static_cast<int>(out->status);
    }
    // Existing WKE platform initialization, on its real owner thread. Never
    // call V8::InitializePlatform/Initialize/Dispose/ShutdownPlatform here.
    if (!::wke::wkeIsInit || ::wke::wkeThreadId != GetCurrentThreadId()
        || !Isolate::GetCurrent())
        return static_cast<int>(out->status);
    // node_is_initialized is the original owner-thread lifecycle state, not
    // an atomic cross-thread embedding API. Inspect it only after this gate.
    if (node_is_initialized) {
        out->status = SOUI_NODE_ARCH_ALREADY_INITIALIZED;
        return static_cast<int>(out->status);
    }

    uv_loop_t* loop = new (std::nothrow) uv_loop_t;
    ArrayBufferAllocator* allocator = new (std::nothrow) ArrayBufferAllocator;
    if (!loop || !allocator) {
        delete loop;
        delete allocator;
        return static_cast<int>(out->status);
    }
    if (uv_loop_init(loop) != 0) {
        delete loop;
        delete allocator;
        return static_cast<int>(out->status);
    }

    const char* argv[] = { "soui-node-architecture-diagnostic", nullptr };
    int argc = 1;
    int exec_argc = 0;
    const char** exec_argv = nullptr;
    Init(&argc, argv, &exec_argc, &exec_argv); // Exact production first Init.
    out->nodeGlobalResourcesUntilProcessExit = 1;

    Isolate::CreateParams params;
    params.array_buffer_allocator = allocator;
    Isolate* isolate = Isolate::New(params);
    if (!isolate) {
        delete[] exec_argv;
        if (uv_loop_close(loop) == 0) {
            delete loop;
            delete allocator;
        } else {
            out->retainedUntilProcessExit = 1;
            out->status = SOUI_NODE_ARCH_CLEANUP_FAILED;
        }
        return static_cast<int>(out->status);
    }

    bool clean = false;
    {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);
        Local<Context> context = Context::New(isolate);
        if (context.IsEmpty()) {
            clean = uv_loop_close(loop) == 0;
        } else {
            Context::Scope context_scope(context);
            soui_node_arch_diagnostic_create = true;
            Environment* env = CreateEnvironment(isolate, loop, context,
                argc, argv, exec_argc, exec_argv); // No replacement process.
            soui_node_arch_diagnostic_create = false;
            SouiNodeArchitectureReadSnapshotV1(out, bytes);
            out->nodeGlobalResourcesUntilProcessExit = 1;
            if (env) {
                SouiNodeArchLoopMembers members = {
                    { reinterpret_cast<uv_handle_t*>(env->immediate_check_handle()),
                      reinterpret_cast<uv_handle_t*>(env->immediate_idle_handle()),
                      reinterpret_cast<uv_handle_t*>(env->idle_prepare_handle()),
                      reinterpret_cast<uv_handle_t*>(env->idle_check_handle()),
                      reinterpret_cast<uv_handle_t*>(env->destroy_ids_idle_handle()) },
                    0, false
                };
                uv_walk(loop, SouiNodeArchCheckLoop, &members);
                if (!members.unexpected && members.seen == 31u) {
                    // The original destructor closes the four registered
                    // handles. CreateEnvironment's fifth idle is not registered.
                    uv_close(members.handles[4], nullptr);
                    FreeEnvironment(env); // Original CleanEnv/CleanupHandles.
                    out->environmentDisposed = 1;
                    clean = uv_loop_close(loop) == 0;
                }
                // Unexpected members retain the real env/loop/isolate/allocator
                // until process exit; do not free live storage or report pass.
            }
        }
    }
    delete[] exec_argv;
    if (!clean) {
        out->retainedUntilProcessExit = 1;
        out->status = SOUI_NODE_ARCH_CLEANUP_FAILED;
        return static_cast<int>(out->status);
    }
    out->privateLoopClosed = 1;
    isolate->Dispose();
    out->privateIsolateDisposed = 1;
    delete allocator;
    delete loop;
    // node_is_initialized intentionally stays true: there is no inverse Init.
    return static_cast<int>(out->status);
}
