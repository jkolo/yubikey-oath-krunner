/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pcsc_worker_pool.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QThread>
#include <QMutexLocker>
#include <limits>

namespace YubiKeyOath {
namespace Daemon {

// ============================================================================
// PcscOperation Implementation
// ============================================================================

PcscOperation::PcscOperation(PcscWorkerPool* pool,
                             QString deviceId,
                             std::function<void()> operation,
                             PcscOperationPriority priority)
    : m_pool(pool)
    , m_deviceId(std::move(deviceId))
    , m_operation(std::move(operation))
    , m_priority(priority)
{
    setAutoDelete(true); // QThreadPool will delete after execution
}

void PcscOperation::run()
{
    // Enforce rate limiting before execution
    const qint64 timeSince = m_pool->timeSinceLastOperation(m_deviceId);
    const qint64 minInterval = PcscWorkerPool::MIN_OPERATION_INTERVAL_MS;

    if (timeSince < minInterval) {
        const qint64 sleepTime = minInterval - timeSince;
        qCDebug(YubiKeyDaemonLog) << "Rate limiting device" << m_deviceId
                                  << "- sleeping for" << sleepTime << "ms";
        QThread::msleep(sleepTime);
    }

    // Record operation start time
    m_pool->recordOperation(m_deviceId);

    // Execute the operation
    // Note: No exception handling - project compiled with -fno-exceptions
    qCDebug(YubiKeyDaemonLog) << "Executing PC/SC operation for device" << m_deviceId
                              << "priority" << static_cast<int>(m_priority);
    m_operation();
    qCDebug(YubiKeyDaemonLog) << "PC/SC operation completed for device" << m_deviceId;
}

// ============================================================================
// PcscWorkerPool Implementation
// ============================================================================

PcscWorkerPool::PcscWorkerPool(QObject *parent)
    : QObject(parent)
    , m_threadPool(new QThreadPool(this))
{
    m_threadPool->setMaxThreadCount(DEFAULT_MAX_THREADS);
    qCInfo(YubiKeyDaemonLog) << "PcscWorkerPool initialized with"
                             << DEFAULT_MAX_THREADS << "worker threads";
}

PcscWorkerPool::~PcscWorkerPool()
{
    qCInfo(YubiKeyDaemonLog) << "PcscWorkerPool shutting down...";
    m_threadPool->waitForDone();
    qCInfo(YubiKeyDaemonLog) << "PcscWorkerPool shutdown complete";
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
        qCWarning(YubiKeyDaemonLog) << "Cannot submit PC/SC operation with empty device ID";
        return;
    }

    qCDebug(YubiKeyDaemonLog) << "Queuing PC/SC operation for device" << deviceId
                              << "priority" << static_cast<int>(priority);

    // Create runnable (will be auto-deleted by thread pool)
    auto* runnable = new PcscOperation(this, deviceId, std::move(operation), priority);

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
    const QMutexLocker locker(&m_rateLimitMutex);
    if (m_deviceLastOperation.remove(deviceId) > 0) {
        qCDebug(YubiKeyDaemonLog) << "Cleared rate limiting history for device" << deviceId;
    }
}

bool PcscWorkerPool::waitForDone(int msecs)
{
    qCDebug(YubiKeyDaemonLog) << "Waiting for all PC/SC operations to complete"
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
        qCWarning(YubiKeyDaemonLog) << "Invalid max thread count" << maxThreads
                                    << "- must be between 1 and 16";
        return;
    }

    qCInfo(YubiKeyDaemonLog) << "Setting max thread count to" << maxThreads;
    m_threadPool->setMaxThreadCount(maxThreads);
}

int PcscWorkerPool::maxThreadCount() const
{
    return m_threadPool->maxThreadCount();
}

qint64 PcscWorkerPool::timeSinceLastOperation(const QString& deviceId) const
{
    const QMutexLocker locker(&m_rateLimitMutex);

    if (!m_deviceLastOperation.contains(deviceId)) {
        // Never accessed - return max value to allow immediate execution
        return std::numeric_limits<qint64>::max();
    }

    const qint64 lastOpTime = m_deviceLastOperation.value(deviceId);
    const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    return currentTime - lastOpTime;
}

void PcscWorkerPool::recordOperation(const QString& deviceId)
{
    const QMutexLocker locker(&m_rateLimitMutex);
    const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    m_deviceLastOperation.insert(deviceId, currentTime);
}

} // namespace Daemon
} // namespace YubiKeyOath
