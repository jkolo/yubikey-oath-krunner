/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <QtConcurrent>
#include "common/async_result.h"
#include "common/result.h"

using namespace YubiKeyOath::Shared;

class TestAsyncResult : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /**
     * @brief Test AsyncResult creation with auto-generated ID
     */
    void testCreateWithAutoId()
    {
        // Create a simple future that returns success
        auto future = QtConcurrent::run([]() -> Result<QString> {
            return Result<QString>::success(QStringLiteral("test-value"));
        });

        auto asyncResult = AsyncResult<QString>::create(future);

        // Verify operation ID was generated
        QVERIFY(!asyncResult.operationId.isEmpty());
        QVERIFY(asyncResult.operationId.length() > 0);

        // Wait for result
        asyncResult.waitForFinished();
        QVERIFY(asyncResult.isFinished());
        QVERIFY(!asyncResult.isCanceled());

        // Verify result value
        auto result = asyncResult.future.result();
        QVERIFY(result.isSuccess());
        QCOMPARE(result.value(), QStringLiteral("test-value"));
    }

    /**
     * @brief Test AsyncResult creation with custom ID
     */
    void testCreateWithCustomId()
    {
        const QString customId = QStringLiteral("my-custom-operation-id");

        auto future = QtConcurrent::run([]() -> Result<int> {
            return Result<int>::success(42);
        });

        auto asyncResult = AsyncResult<int>::create(customId, future);

        // Verify custom ID was used
        QCOMPARE(asyncResult.operationId, customId);

        // Wait for result
        asyncResult.waitForFinished();

        auto result = asyncResult.future.result();
        QVERIFY(result.isSuccess());
        QCOMPARE(result.value(), 42);
    }

    /**
     * @brief Test AsyncResult with error result
     */
    void testAsyncResultWithError()
    {
        auto future = QtConcurrent::run([]() -> Result<QString> {
            return Result<QString>::error(QStringLiteral("Operation failed"));
        });

        auto asyncResult = AsyncResult<QString>::create(future);
        asyncResult.waitForFinished();

        auto result = asyncResult.future.result();
        QVERIFY(!result.isSuccess());
        QCOMPARE(result.error(), QStringLiteral("Operation failed"));
    }

    /**
     * @brief Test AsyncResult<void> specialization with success
     */
    void testVoidAsyncResultSuccess()
    {
        auto future = QtConcurrent::run([]() -> Result<void> {
            return Result<void>::success();
        });

        auto asyncResult = AsyncResult<void>::create(future);

        QVERIFY(!asyncResult.operationId.isEmpty());
        asyncResult.waitForFinished();
        QVERIFY(asyncResult.isFinished());

        auto result = asyncResult.future.result();
        QVERIFY(result.isSuccess());
    }

    /**
     * @brief Test AsyncResult<void> specialization with error
     */
    void testVoidAsyncResultError()
    {
        auto future = QtConcurrent::run([]() -> Result<void> {
            return Result<void>::error(QStringLiteral("Void operation failed"));
        });

        auto asyncResult = AsyncResult<void>::create(future);
        asyncResult.waitForFinished();

        auto result = asyncResult.future.result();
        QVERIFY(!result.isSuccess());
        QCOMPARE(result.error(), QStringLiteral("Void operation failed"));
    }

    /**
     * @brief Test unique operation IDs
     */
    void testUniqueOperationIds()
    {
        QSet<QString> ids;

        // Create 100 async results and verify all IDs are unique
        for (int i = 0; i < 100; ++i) {
            auto future = QtConcurrent::run([]() -> Result<int> {
                return Result<int>::success(0);
            });

            auto asyncResult = AsyncResult<int>::create(future);
            QVERIFY(!ids.contains(asyncResult.operationId));
            ids.insert(asyncResult.operationId);
        }

        QCOMPARE(ids.size(), 100);
    }

    /**
     * @brief Test isFinished() state transitions
     */
    void testIsFinishedStateTransition()
    {
        QEventLoop loop;
        bool operationStarted = false;

        auto future = QtConcurrent::run([&operationStarted]() -> Result<QString> {
            operationStarted = true;
            QThread::msleep(50); // Simulate work
            return Result<QString>::success(QStringLiteral("done"));
        });

        auto asyncResult = AsyncResult<QString>::create(future);

        // Initially may or may not be finished (race condition)
        // But after waiting, must be finished
        asyncResult.waitForFinished();
        QVERIFY(asyncResult.isFinished());
        QVERIFY(operationStarted);
    }

    /**
     * @brief Test long-running operation tracking
     */
    void testLongRunningOperation()
    {
        auto future = QtConcurrent::run([]() -> Result<QString> {
            // Simulate long operation
            QThread::msleep(100);
            return Result<QString>::success(QStringLiteral("completed"));
        });

        auto asyncResult = AsyncResult<QString>::create(future);

        // Should not be finished immediately
        // (Note: This is a race, but 100ms should be long enough)
        QThread::msleep(10); // Give thread time to start
        // Don't assert !isFinished() as it's a race

        // Wait and verify completion
        asyncResult.waitForFinished();
        QVERIFY(asyncResult.isFinished());

        auto result = asyncResult.future.result();
        QVERIFY(result.isSuccess());
        QCOMPARE(result.value(), QStringLiteral("completed"));
    }

    /**
     * @brief Test multiple operations with different types
     */
    void testMultipleTypedOperations()
    {
        // String operation
        auto stringFuture = QtConcurrent::run([]() -> Result<QString> {
            return Result<QString>::success(QStringLiteral("text"));
        });
        auto stringAsync = AsyncResult<QString>::create(stringFuture);

        // Int operation
        auto intFuture = QtConcurrent::run([]() -> Result<int> {
            return Result<int>::success(123);
        });
        auto intAsync = AsyncResult<int>::create(intFuture);

        // Void operation
        auto voidFuture = QtConcurrent::run([]() -> Result<void> {
            return Result<void>::success();
        });
        auto voidAsync = AsyncResult<void>::create(voidFuture);

        // Wait for all
        stringAsync.waitForFinished();
        intAsync.waitForFinished();
        voidAsync.waitForFinished();

        // Verify all succeeded
        QVERIFY(stringAsync.future.result().isSuccess());
        QVERIFY(intAsync.future.result().isSuccess());
        QVERIFY(voidAsync.future.result().isSuccess());

        // Verify all have unique IDs
        QVERIFY(stringAsync.operationId != intAsync.operationId);
        QVERIFY(stringAsync.operationId != voidAsync.operationId);
        QVERIFY(intAsync.operationId != voidAsync.operationId);
    }
};

QTEST_MAIN(TestAsyncResult)
#include "test_async_result.moc"
