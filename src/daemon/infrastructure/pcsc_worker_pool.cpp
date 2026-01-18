/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pcsc_worker_pool.h"
#include "../logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

// ============================================================================
// PcscOperation Implementation
// ============================================================================

PcscOperation::PcscOperation(QString deviceId,
                             std::function<void()> operation,
                             PcscOperationPriority priority)
    : m_deviceId(std::move(deviceId))
    , m_operation(std::move(operation))
    , m_priority(priority)
{
    setAutoDelete(true); // QThreadPool will delete after execution
}

void PcscOperation::run()
{
    // Rate limiting is now handled at YkOathSession level (configurable via PcscRateLimitMs).
    // This eliminates redundant delays and centralizes rate limit configuration.

    // Execute the operation
    // Note: No exception handling - project compiled with -fno-exceptions
    qCDebug(OathDaemonLog) << "Executing PC/SC operation for device" << m_deviceId
                              << "priority" << static_cast<int>(m_priority);
    m_operation();
    qCDebug(OathDaemonLog) << "PC/SC operation completed for device" << m_deviceId;
}

// ============================================================================
// PcscWorkerPool Implementation
// ============================================================================

PcscWorkerPool::PcscWorkerPool(QObject *parent)
    : QObject(parent)
    , m_threadPool(new QThreadPool(this))
{
    m_threadPool->setMaxThreadCount(DEFAULT_MAX_THREADS);
    qCInfo(OathDaemonLog) << "PcscWorkerPool initialized with"
                             << DEFAULT_MAX_THREADS << "worker threads";
}

PcscWorkerPool::~PcscWorkerPool()
{
    qCInfo(OathDaemonLog) << "PcscWorkerPool shutting down...";
    m_threadPool->waitForDone();
    qCInfo(OathDaemonLog) << "PcscWorkerPool shutdown complete";
}

PcscWorkerPool& PcscWorkerPool::instance()
{
    static PcscWorkerPool instance;
    return instance;
}

void PcscWorkerPool::submit(const QString& deviceId,
                             std::function<void()> operation,
                             PcscOperationPriority priority)
{
    if (deviceId.isEmpty()) {
        qCWarning(OathDaemonLog) << "Cannot submit PC/SC operation with empty device ID";
        return;
    }

    qCDebug(OathDaemonLog) << "Queuing PC/SC operation for device" << deviceId
                              << "priority" << static_cast<int>(priority);

    // Create runnable (will be auto-deleted by thread pool)
    auto* runnable = new PcscOperation(deviceId, std::move(operation), priority);

    // Map priority to Qt's priority levels
    // Note: Qt doesn't support fine-grained priority, so we map to 3 levels
    int qtPriority = 0; // Normal priority by default
    switch (priority) {
    case PcscOperationPriority::Background:
        qtPriority = -1; // Low priority (processed after normal)
        break;
    case PcscOperationPriority::Normal:
        qtPriority = 0;  // Normal priority
        break;
    case PcscOperationPriority::UserInteraction:
        qtPriority = 1;  // High priority (processed before normal)
        break;
    }

    m_threadPool->start(runnable, qtPriority);
}

void PcscWorkerPool::clearDeviceHistory(const QString& deviceId)
{
    // No-op - rate limiting is now handled at YkOathSession level.
    // Kept for API compatibility.
    Q_UNUSED(deviceId)
}

bool PcscWorkerPool::waitForDone(int msecs)
{
    qCDebug(OathDaemonLog) << "Waiting for all PC/SC operations to complete"
                              << "(timeout:" << msecs << "ms)";
    return m_threadPool->waitForDone(msecs);
}

int PcscWorkerPool::activeThreadCount() const
{
    return m_threadPool->activeThreadCount();
}

void PcscWorkerPool::setMaxThreadCount(int maxThreads)
{
    if (maxThreads < 1 || maxThreads > 16) {
        qCWarning(OathDaemonLog) << "Invalid max thread count" << maxThreads
                                    << "- must be between 1 and 16";
        return;
    }

    qCInfo(OathDaemonLog) << "Setting max thread count to" << maxThreads;
    m_threadPool->setMaxThreadCount(maxThreads);
}

int PcscWorkerPool::maxThreadCount() const
{
    return m_threadPool->maxThreadCount();
}

} // namespace Daemon
} // namespace YubiKeyOath
