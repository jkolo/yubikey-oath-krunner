/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QMutex>
#include <QThreadPool>
#include <QMap>
#include <QString>
#include <QRunnable>
#include <functional>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Priority levels for PC/SC operations
 *
 * Controls queuing order in the worker pool. Higher priority operations
 * are executed first when multiple operations are pending.
 */
enum class PcscOperationPriority {
    Background = 0,      ///< Background operations (credential refresh, monitoring)
    Normal = 10,         ///< Regular operations (device connection, initial fetch)
    UserInteraction = 20 ///< User-initiated operations (generate code, add credential)
};

/**
 * @brief Dedicated thread pool for PC/SC operations
 *
 * Provides:
 * - Per-device rate limiting (50ms minimum between operations)
 * - Priority-based queuing
 * - Thread pool size control (max 4 workers)
 * - Device-safe operation serialization
 *
 * Singleton pattern ensures global coordination of PC/SC access.
 * All device operations should go through this pool to prevent:
 * - Communication errors from rapid PC/SC calls
 * - Reader/card conflicts from concurrent access
 * - System resource exhaustion
 *
 * Thread safety: All methods are thread-safe.
 */
class PcscWorkerPool : public QObject {
    Q_OBJECT

    // Allow PcscOperation to access rate limiting methods
    friend class PcscOperation;

public:
    /**
     * @brief Gets the global worker pool instance
     * @return Reference to singleton instance
     */
    static PcscWorkerPool& instance();

    /**
     * @brief Submits a PC/SC operation for execution
     *
     * The operation will be queued and executed when:
     * 1. A worker thread becomes available
     * 2. At least 50ms has elapsed since last operation on this device
     * 3. All higher priority operations are processed
     *
     * @param deviceId Device identifier (for rate limiting)
     * @param operation Function to execute (may throw exceptions)
     * @param priority Operation priority level
     *
     * @note The operation runs on a worker thread, not the caller's thread.
     *       Use signals/slots or QMetaObject::invokeMethod for cross-thread communication.
     *
     * Usage:
     * @code
     * PcscWorkerPool::instance().submit("device-123", [device]() {
     *     device->performPcscOperation();
     * }, PcscOperationPriority::UserInteraction);
     * @endcode
     */
    void submit(const QString& deviceId,
                std::function<void()> operation,
                PcscOperationPriority priority = PcscOperationPriority::Normal);

    /**
     * @brief Clears rate limiting history for a device
     *
     * Call when device is disconnected to free memory.
     * Does NOT cancel pending operations for this device.
     *
     * @param deviceId Device identifier
     */
    void clearDeviceHistory(const QString& deviceId);

    /**
     * @brief Waits for all pending operations to complete
     *
     * Blocks until all queued operations finish execution.
     * Used primarily for graceful shutdown.
     *
     * @param msecs Maximum wait time in milliseconds (-1 = wait forever)
     * @return true if all operations completed, false if timeout
     */
    bool waitForDone(int msecs = -1);

    /**
     * @brief Gets number of active worker threads
     * @return Current number of threads actively executing operations
     */
    int activeThreadCount() const;

    /**
     * @brief Sets maximum number of worker threads
     *
     * Default is 4. Reducing below current active threads will not
     * terminate running operations, only prevent new ones from starting.
     *
     * @param maxThreads Maximum threads (1-16 recommended)
     */
    void setMaxThreadCount(int maxThreads);

    /**
     * @brief Gets maximum number of worker threads
     * @return Configured max thread count
     */
    int maxThreadCount() const;

private:
    // Singleton - private constructor
    explicit PcscWorkerPool(QObject *parent = nullptr);
    ~PcscWorkerPool() override;

    // Prevent copying
    Q_DISABLE_COPY(PcscWorkerPool)

    /**
     * @brief Gets time since last operation on device
     * @param deviceId Device identifier
     * @return Milliseconds since last operation, or LLONG_MAX if never accessed
     */
    qint64 timeSinceLastOperation(const QString& deviceId) const;

    /**
     * @brief Records operation timestamp for device
     * @param deviceId Device identifier
     */
    void recordOperation(const QString& deviceId);

    QThreadPool *m_threadPool;                  ///< Underlying thread pool
    mutable QMutex m_rateLimitMutex;            ///< Protects rate limiting map
    QMap<QString, qint64> m_deviceLastOperation; ///< Last operation timestamp per device

    static constexpr int DEFAULT_MAX_THREADS = 4;
    static constexpr qint64 MIN_OPERATION_INTERVAL_MS = 50; ///< 50ms rate limit
};

/**
 * @brief Internal QRunnable wrapper for rate-limited operations
 *
 * Handles:
 * - Enforcing rate limiting before execution
 * - Priority-based scheduling
 * - Exception catching and logging
 *
 * @note For internal use by PcscWorkerPool only
 */
class PcscOperation : public QRunnable {
public:
    PcscOperation(PcscWorkerPool* pool,
                  QString deviceId,
                  std::function<void()> operation,
                  PcscOperationPriority priority);

    void run() override;

    // Enable priority-based queue sorting
    PcscOperationPriority priority() const { return m_priority; }

private:
    PcscWorkerPool* m_pool;
    QString m_deviceId;
    std::function<void()> m_operation;
    PcscOperationPriority m_priority;
};

} // namespace Daemon
} // namespace YubiKeyOath
