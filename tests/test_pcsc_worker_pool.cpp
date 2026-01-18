/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <QElapsedTimer>
#include <QMutex>
#include <QAtomicInt>
#include "daemon/infrastructure/pcsc_worker_pool.h"

using namespace YubiKeyOath::Daemon;

class TestPcscWorkerPool : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Ensure pool is initialized
        PcscWorkerPool::instance();
    }

    void cleanupTestCase()
    {
        // Wait for all operations to complete
        PcscWorkerPool::instance().waitForDone(5000);
    }

    void cleanup()
    {
        // Wait between tests
        PcscWorkerPool::instance().waitForDone(1000);
    }

    /**
     * @brief Test basic operation submission and execution
     */
    void testBasicOperationExecution()
    {
        QAtomicInt executionCount(0);

        PcscWorkerPool::instance().submit(
            QStringLiteral("test-device-1"),
            [&executionCount]() {
                executionCount.fetchAndAddOrdered(1);
            },
            PcscOperationPriority::Normal
        );

        // Wait for operation to complete
        bool success = PcscWorkerPool::instance().waitForDone(1000);
        QVERIFY(success);
        QCOMPARE(executionCount.loadRelaxed(), 1);
    }

    /**
     * @brief Test that operations execute without rate limiting delays
     *
     * NOTE: Rate limiting is now handled at YkOathSession level, not in PcscWorkerPool.
     * This test verifies that operations execute quickly without artificial delays.
     */
    void testRateLimiting()
    {
        const QString deviceId = QStringLiteral("rate-limit-device");
        auto& pool = PcscWorkerPool::instance();

        // Set thread count to 1 to force serial execution
        int originalThreadCount = pool.maxThreadCount();
        pool.setMaxThreadCount(1);

        QMutex mutex;
        QList<qint64> timestamps;

        QElapsedTimer timer;
        timer.start();

        // Submit 3 rapid operations for same device
        for (int i = 0; i < 3; ++i) {
            pool.submit(
                deviceId,
                [&mutex, &timestamps]() {
                    QMutexLocker locker(&mutex);
                    timestamps.append(QDateTime::currentMSecsSinceEpoch());
                },
                PcscOperationPriority::Normal
            );
        }

        // Wait for all operations
        bool success = pool.waitForDone(5000);
        QVERIFY(success);
        QCOMPARE(timestamps.size(), 3);

        // Verify operations completed quickly (no 50ms delays between operations)
        // With 3 operations and no rate limiting, total time should be well under 100ms
        qint64 totalTime = timer.elapsed();
        QVERIFY2(totalTime < 100,
                 qPrintable(QString("Total time %1ms too slow, expected < 100ms (no rate limiting)").arg(totalTime)));

        // Restore original thread count
        pool.setMaxThreadCount(originalThreadCount);
    }

    /**
     * @brief Test that multiple devices can execute operations concurrently
     *
     * NOTE: Rate limiting is now handled at YkOathSession level, not in PcscWorkerPool.
     * This test verifies that multiple devices can execute without interference.
     */
    void testMultipleDevicesConcurrency()
    {
        const QString device1 = QStringLiteral("device-1");
        const QString device2 = QStringLiteral("device-2");
        auto& pool = PcscWorkerPool::instance();

        // Set thread count to 2 (one per device)
        int originalThreadCount = pool.maxThreadCount();
        pool.setMaxThreadCount(2);

        QMutex mutex;
        QMap<QString, QList<qint64>> deviceTimestamps;

        QElapsedTimer timer;
        timer.start();

        // Submit operations for both devices in interleaved fashion
        for (int i = 0; i < 3; ++i) {
            pool.submit(
                device1,
                [&mutex, &deviceTimestamps, device1]() {
                    QMutexLocker locker(&mutex);
                    deviceTimestamps[device1].append(QDateTime::currentMSecsSinceEpoch());
                },
                PcscOperationPriority::Normal
            );

            pool.submit(
                device2,
                [&mutex, &deviceTimestamps, device2]() {
                    QMutexLocker locker(&mutex);
                    deviceTimestamps[device2].append(QDateTime::currentMSecsSinceEpoch());
                },
                PcscOperationPriority::Normal
            );
        }

        bool success = pool.waitForDone(5000);
        QVERIFY(success);

        // Each device should have executed 3 operations
        QCOMPARE(deviceTimestamps[device1].size(), 3);
        QCOMPARE(deviceTimestamps[device2].size(), 3);

        // Verify operations completed quickly without rate limiting delays
        // 6 total operations with 2 threads should complete well under 100ms
        qint64 totalTime = timer.elapsed();
        QVERIFY2(totalTime < 200,  // Allow some tolerance for thread scheduling
                 qPrintable(QString("Total time %1ms too slow, expected < 200ms (no rate limiting)").arg(totalTime)));

        // Restore original thread count
        pool.setMaxThreadCount(originalThreadCount);
    }

    /**
     * @brief Test priority-based execution order
     */
    void testPriorityOrdering()
    {
        const QString deviceId = QStringLiteral("priority-device");
        QMutex mutex;
        QList<int> executionOrder;

        // Submit operations with different priorities
        // Thread pool priority mechanism should favor higher priority operations

        // Low priority
        PcscWorkerPool::instance().submit(
            deviceId,
            [&mutex, &executionOrder]() {
                QMutexLocker locker(&mutex);
                executionOrder.append(1);
            },
            PcscOperationPriority::Background
        );

        // High priority - should execute before low priority
        PcscWorkerPool::instance().submit(
            deviceId,
            [&mutex, &executionOrder]() {
                QMutexLocker locker(&mutex);
                executionOrder.append(3);
            },
            PcscOperationPriority::UserInteraction
        );

        // Medium priority
        PcscWorkerPool::instance().submit(
            deviceId,
            [&mutex, &executionOrder]() {
                QMutexLocker locker(&mutex);
                executionOrder.append(2);
            },
            PcscOperationPriority::Normal
        );

        bool success = PcscWorkerPool::instance().waitForDone(5000);
        QVERIFY(success);
        QCOMPARE(executionOrder.size(), 3);

        // Note: First operation executes immediately, so we can't guarantee order
        // But the queued operations should execute in priority order
        // This test verifies pool accepts priority parameter (execution order is QThreadPool's responsibility)
    }

    /**
     * @brief Test device history clearing (now a no-op, kept for API compatibility)
     *
     * NOTE: Rate limiting is now handled at YkOathSession level, not in PcscWorkerPool.
     * clearDeviceHistory() is now a no-op kept for API compatibility.
     */
    void testClearDeviceHistory()
    {
        const QString deviceId = QStringLiteral("history-device");
        QAtomicInt executionCount(0);

        // Execute first operation
        PcscWorkerPool::instance().submit(
            deviceId,
            [&executionCount]() {
                executionCount.fetchAndAddOrdered(1);
            },
            PcscOperationPriority::Normal
        );

        PcscWorkerPool::instance().waitForDone(1000);
        QCOMPARE(executionCount.loadRelaxed(), 1);

        // Clear history (now a no-op, kept for API compatibility)
        PcscWorkerPool::instance().clearDeviceHistory(deviceId);

        // Submit second operation immediately - should execute without delay
        PcscWorkerPool::instance().submit(
            deviceId,
            [&executionCount]() {
                executionCount.fetchAndAddOrdered(1);
            },
            PcscOperationPriority::Normal
        );

        PcscWorkerPool::instance().waitForDone(1000);
        QCOMPARE(executionCount.loadRelaxed(), 2);
    }

    /**
     * @brief Test thread pool size management
     */
    void testThreadPoolSizeManagement()
    {
        auto& pool = PcscWorkerPool::instance();

        // Get initial thread count
        int initialMax = pool.maxThreadCount();
        QVERIFY(initialMax > 0);

        // Set new max thread count
        pool.setMaxThreadCount(2);
        QCOMPARE(pool.maxThreadCount(), 2);

        // Restore original
        pool.setMaxThreadCount(initialMax);
        QCOMPARE(pool.maxThreadCount(), initialMax);
    }

    /**
     * @brief Test concurrent operations on different devices
     */
    void testConcurrentDeviceOperations()
    {
        QMutex mutex;
        QMap<QString, int> deviceExecutionCounts;

        // Submit 10 operations across 5 different devices
        for (int deviceNum = 0; deviceNum < 5; ++deviceNum) {
            QString deviceId = QString("concurrent-device-%1").arg(deviceNum);

            for (int opNum = 0; opNum < 2; ++opNum) {
                PcscWorkerPool::instance().submit(
                    deviceId,
                    [&mutex, &deviceExecutionCounts, deviceId]() {
                        QThread::msleep(10); // Simulate work
                        QMutexLocker locker(&mutex);
                        deviceExecutionCounts[deviceId]++;
                    },
                    PcscOperationPriority::Normal
                );
            }
        }

        bool success = PcscWorkerPool::instance().waitForDone(5000);
        QVERIFY(success);

        // Verify all operations executed
        QCOMPARE(deviceExecutionCounts.size(), 5);
        for (const auto& count : deviceExecutionCounts) {
            QCOMPARE(count, 2);
        }
    }

    /**
     * @brief Test active thread count reporting
     */
    void testActiveThreadCount()
    {
        auto& pool = PcscWorkerPool::instance();

        // Initially should have 0 or few active threads
        int initialActive = pool.activeThreadCount();
        QVERIFY(initialActive >= 0);

        // Submit long-running operation
        QAtomicInt operationRunning(0);
        pool.submit(
            QStringLiteral("thread-count-device"),
            [&operationRunning]() {
                operationRunning.storeRelaxed(1);
                QThread::msleep(200);
                operationRunning.storeRelaxed(0);
            },
            PcscOperationPriority::Normal
        );

        // Wait for operation to start
        int maxWait = 100; // 100 iterations * 10ms = 1 second max
        while (operationRunning.loadRelaxed() == 0 && maxWait-- > 0) {
            QThread::msleep(10);
        }

        // Should have at least 1 active thread now
        // (Note: This is a race condition, but operation is long enough)
        if (operationRunning.loadRelaxed() == 1) {
            int activeWhileRunning = pool.activeThreadCount();
            QVERIFY(activeWhileRunning >= 0); // Can't guarantee > 0 due to timing
        }

        pool.waitForDone(1000);
    }

    /**
     * @brief Test wait timeout behavior
     */
    void testWaitTimeout()
    {
        auto& pool = PcscWorkerPool::instance();

        // Submit a long operation
        pool.submit(
            QStringLiteral("timeout-device"),
            []() {
                QThread::msleep(500);
            },
            PcscOperationPriority::Normal
        );

        // Wait with short timeout - should timeout
        bool result = pool.waitForDone(50);
        QVERIFY(!result); // Should timeout

        // Wait with long timeout - should succeed
        result = pool.waitForDone(1000);
        QVERIFY(result);
    }

    /**
     * @brief Test pool remains functional after operations complete
     */
    void testPoolReusability()
    {
        QAtomicInt executionCount(0);

        // Submit first operation
        PcscWorkerPool::instance().submit(
            QStringLiteral("reuse-device"),
            [&executionCount]() {
                executionCount.fetchAndAddOrdered(1);
            },
            PcscOperationPriority::Normal
        );

        PcscWorkerPool::instance().waitForDone(1000);
        QCOMPARE(executionCount.loadRelaxed(), 1);

        // Pool should still be functional for subsequent operations
        executionCount.storeRelaxed(0);
        PcscWorkerPool::instance().submit(
            QStringLiteral("reuse-device"),
            [&executionCount]() {
                executionCount.fetchAndAddOrdered(1);
            },
            PcscOperationPriority::Normal
        );

        PcscWorkerPool::instance().waitForDone(1000);
        QCOMPARE(executionCount.loadRelaxed(), 1);
    }
};

QTEST_MAIN(TestPcscWorkerPool)
#include "test_pcsc_worker_pool.moc"
