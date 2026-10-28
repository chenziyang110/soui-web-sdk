// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
static void bindPaintLifetime(const std::shared_ptr<PaintState>& state, CWebView* target)
{
    auto& runtime = content::paintLifetimeRuntime();
    std::lock_guard<std::mutex> lock(runtime.mutex);
    state->target = target;
    state->owner = blink::Platform::current()->mainThread();
    state->onIdle = queuePaintClose;
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
static void invalidatePaintLifetime(const std::shared_ptr<PaintState>& state)
{
    auto& runtime = content::paintLifetimeRuntime();
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (state->policy.active || state->policy.nativeFailed)
        std::terminate(); // A raw delete may never unwind a live paint stack.
    if (state->policy.pending != PaintKind::none)
        --runtime.pending;
    state->policy.invalidate();
    state->target = nullptr;
    state->owner = nullptr;
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
content::PaintClosePermit CWebView::acquirePaintClosePermit(content::PaintCloseKind kind)
{
    content::PaintClosePermit permit(m_paintLifetime, kind);
    if (!permit)
        queuePaintClose(m_paintLifetime);
    return permit;
}
