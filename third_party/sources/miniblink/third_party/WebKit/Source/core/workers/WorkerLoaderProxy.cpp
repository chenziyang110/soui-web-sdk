// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "core/workers/WorkerLoaderProxy.h"

#include "core/dom/ExecutionContextTask.h"

namespace blink {

WorkerLoaderProxy::WorkerLoaderProxy(WorkerLoaderProxyProvider* loaderProxyProvider)
    : m_loaderProxyProvider(loaderProxyProvider)
{
}

WorkerLoaderProxy::~WorkerLoaderProxy()
{
    ASSERT(!m_loaderProxyProvider);
}

void WorkerLoaderProxy::detachProvider(WorkerLoaderProxyProvider* proxyProvider)
{
    MutexLocker locker(m_lock);
    ASSERT_UNUSED(proxyProvider, proxyProvider == m_loaderProxyProvider);
    m_loaderProxyProvider = nullptr;
}

void WorkerLoaderProxy::postTaskToLoader(PassOwnPtr<ExecutionContextTask> task)
{
    RefPtr<WorkerShutdownRecord> record = m_shutdownRecord;
    if (!record) { // Preserve the unsupported legacy provider route.
        MutexLocker locker(m_lock);
        if (m_loaderProxyProvider) m_loaderProxyProvider->postTaskToLoader(task);
        return;
    }
    WorkerLoaderProxyProvider* provider = nullptr;
    {
        MutexLocker locker(m_lock);
        if (m_loaderProxyProvider && record->beginCallback()) provider = m_loaderProxyProvider;
    }
    if (!provider) return; // PassOwnPtr retires only after the provider mutex scope.
    struct Borrow {
        explicit Borrow(WorkerShutdownRecord* record) : record(record) { }
        ~Borrow() { record->finishCallback(); scheduleWorkerRetirementPoll(); }
        RefPtr<WorkerShutdownRecord> record;
    } borrow(record.get());
    provider->postTaskToLoader(task); // No provider mutex spans payload/target access.
}

bool WorkerLoaderProxy::postTaskToWorkerGlobalScope(PassOwnPtr<ExecutionContextTask> task)
{
    RefPtr<WorkerShutdownRecord> record = m_shutdownRecord;
    if (!record) {
        MutexLocker locker(m_lock);
        return m_loaderProxyProvider && m_loaderProxyProvider->postTaskToWorkerGlobalScope(task);
    }
    WorkerLoaderProxyProvider* provider = nullptr;
    {
        MutexLocker locker(m_lock);
        if (m_loaderProxyProvider && record->beginCallback()) provider = m_loaderProxyProvider;
    }
    if (!provider) return false;
    struct Borrow {
        explicit Borrow(WorkerShutdownRecord* record) : record(record) { }
        ~Borrow() { record->finishCallback(); scheduleWorkerRetirementPoll(); }
        RefPtr<WorkerShutdownRecord> record;
    } borrow(record.get());
    return provider->postTaskToWorkerGlobalScope(task);
}

} // namespace blink
