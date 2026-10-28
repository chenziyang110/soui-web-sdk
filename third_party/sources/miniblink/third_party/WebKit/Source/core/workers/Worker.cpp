// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "platform/network/LocalResourceAdmission.h"
#include "core/workers/Worker.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/frame/UseCounter.h"
#include "core/loader/InspectorResourcePolicy.h"
#include "core/workers/WorkerGlobalScopeProxy.h"
#include "core/workers/WorkerGlobalScopeProxyProvider.h"

namespace blink {

Worker::Worker(ExecutionContext* context)
    : InProcessWorkerBase(context)
{
}

PassRefPtrWillBeRawPtr<Worker> Worker::create(ExecutionContext* context, const String& url, ExceptionState& exceptionState)
{
    ASSERT(isMainThread());
    Document* document = toDocument(context);
    if (hasLocalResourceOwner(document)) {
        exceptionState.throwSecurityError("Workers are unsupported by this owned local static resource mode.");
        return nullptr;
    }
    if (InspectorResourcePolicy::isControlled(document)) {
        exceptionState.throwSecurityError("This operation is disabled in the controlled inspector.");
        return nullptr;
    }

    UseCounter::count(context, UseCounter::WorkerStart);
    if (!document->page()) {
        exceptionState.throwDOMException(InvalidAccessError, "The context provided is invalid.");
        return nullptr;
    }
    RefPtrWillBeRawPtr<Worker> worker = adoptRefWillBeNoop(new Worker(context));
    if (worker->initialize(context, url, exceptionState))
        return worker.release();
    return nullptr;
}

Worker::~Worker()
{
    ASSERT(isMainThread());
}

const AtomicString& Worker::interfaceName() const
{
    return EventTargetNames::Worker;
}

WorkerGlobalScopeProxy* Worker::createWorkerGlobalScopeProxy(ExecutionContext* context)
{
    Document* document = toDocument(context);
    WorkerGlobalScopeProxyProvider* proxyProvider = WorkerGlobalScopeProxyProvider::from(*document->page());
    ASSERT(proxyProvider);
    return proxyProvider->createWorkerGlobalScopeProxy(this);
}

} // namespace blink
