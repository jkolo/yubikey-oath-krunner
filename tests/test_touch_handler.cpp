/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemon/workflows/touch_handler.h"
#include <QSignalSpy>
#include <QTest>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Unit tests for TouchHandler
 *
 * Tests touch timeout management and state tracking
 */
class TestTouchHandler : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    // State management tests
    void testStartTouchOperation_SetsState();
    void testStartTouchOperation_WithZeroTimeout();
    void testCancelTouchOperation_ClearsState();
    void testIsTouchActive_ReturnsCorrectState();
    void testWaitingCredential_ReturnsCorrectName();

    // Timeout tests
    void testTouchTimeout_EmitsSignal();
    void testTouchTimeout_ClearsState();
    void testMultipleStarts_ResetsTimer();
    void testCancelBeforeTimeout_NoSignal();

private:
    TouchHandler *m_handler = nullptr;
};

void TestTouchHandler::initTestCase()
{
    // No global initialization needed
}

void TestTouchHandler::cleanup()
{
    delete m_handler;
    m_handler = nullptr;
}

// ========== State Management Tests ==========

void TestTouchHandler::testStartTouchOperation_SetsState()
{
    m_handler = new TouchHandler(this);

    QVERIFY(!m_handler->isTouchActive());
    QVERIFY(m_handler->waitingCredential().isEmpty());

    m_handler->startTouchOperation(QStringLiteral("Google:user@test"), 15);

    QVERIFY(m_handler->isTouchActive());
    QCOMPARE(m_handler->waitingCredential(), QStringLiteral("Google:user@test"));
}

void TestTouchHandler::testStartTouchOperation_WithZeroTimeout()
{
    m_handler = new TouchHandler(this);

    m_handler->startTouchOperation(QStringLiteral("GitHub:user"), 0);

    QVERIFY(m_handler->isTouchActive());
    QCOMPARE(m_handler->waitingCredential(), QStringLiteral("GitHub:user"));

    // With zero timeout, should not timeout automatically
    QTest::qWait(100);
    QVERIFY(m_handler->isTouchActive()); // Still active
}

void TestTouchHandler::testCancelTouchOperation_ClearsState()
{
    m_handler = new TouchHandler(this);

    m_handler->startTouchOperation(QStringLiteral("Amazon:user"), 10);
    QVERIFY(m_handler->isTouchActive());

    m_handler->cancelTouchOperation();

    QVERIFY(!m_handler->isTouchActive());
    QVERIFY(m_handler->waitingCredential().isEmpty());
}

void TestTouchHandler::testIsTouchActive_ReturnsCorrectState()
{
    m_handler = new TouchHandler(this);

    // Initially not active
    QVERIFY(!m_handler->isTouchActive());

    // Active after start
    m_handler->startTouchOperation(QStringLiteral("Test:cred"), 5);
    QVERIFY(m_handler->isTouchActive());

    // Not active after cancel
    m_handler->cancelTouchOperation();
    QVERIFY(!m_handler->isTouchActive());
}

void TestTouchHandler::testWaitingCredential_ReturnsCorrectName()
{
    m_handler = new TouchHandler(this);

    // Empty initially
    QVERIFY(m_handler->waitingCredential().isEmpty());

    // Returns correct name after start
    const QString credName = QStringLiteral("Microsoft:work@example.com");
    m_handler->startTouchOperation(credName, 10);
    QCOMPARE(m_handler->waitingCredential(), credName);

    // Empty after cancel
    m_handler->cancelTouchOperation();
    QVERIFY(m_handler->waitingCredential().isEmpty());
}

// ========== Timeout Tests ==========

void TestTouchHandler::testTouchTimeout_EmitsSignal()
{
    m_handler = new TouchHandler(this);

    QSignalSpy spy(m_handler, &TouchHandler::touchTimedOut);

    const QString credName = QStringLiteral("Facebook:user@test");
    m_handler->startTouchOperation(credName, 1); // 1 second timeout

    // Wait for timeout (1000ms + margin)
    QVERIFY(spy.wait(1500));

    // Check signal was emitted with correct argument
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), credName);
}

void TestTouchHandler::testTouchTimeout_ClearsState()
{
    m_handler = new TouchHandler(this);

    QSignalSpy spy(m_handler, &TouchHandler::touchTimedOut);

    m_handler->startTouchOperation(QStringLiteral("Dropbox:test"), 1);

    // Wait for timeout
    QVERIFY(spy.wait(1500));

    // State should be cleared after timeout
    QVERIFY(!m_handler->isTouchActive());
    QVERIFY(m_handler->waitingCredential().isEmpty());
}

void TestTouchHandler::testMultipleStarts_ResetsTimer()
{
    m_handler = new TouchHandler(this);

    QSignalSpy spy(m_handler, &TouchHandler::touchTimedOut);

    // Start first operation with 2 second timeout
    m_handler->startTouchOperation(QStringLiteral("First:cred"), 2);
    QCOMPARE(m_handler->waitingCredential(), QStringLiteral("First:cred"));

    // Wait 1 second
    QTest::qWait(1000);

    // Start second operation - should reset timer
    m_handler->startTouchOperation(QStringLiteral("Second:cred"), 2);
    QCOMPARE(m_handler->waitingCredential(), QStringLiteral("Second:cred"));

    // Wait another 1.5 seconds (total 2.5s from first start, 1.5s from second)
    QTest::qWait(1500);

    // Should not have timed out yet (only 1.5s since second start)
    QCOMPARE(spy.count(), 0);

    // Wait for second timeout to occur (another 1s)
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Second:cred"));
}

void TestTouchHandler::testCancelBeforeTimeout_NoSignal()
{
    m_handler = new TouchHandler(this);

    QSignalSpy spy(m_handler, &TouchHandler::touchTimedOut);

    m_handler->startTouchOperation(QStringLiteral("Test:user"), 2);

    // Cancel before timeout
    QTest::qWait(500);
    m_handler->cancelTouchOperation();

    // Wait past original timeout time
    QTest::qWait(2000);

    // Signal should not have been emitted
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestTouchHandler)
#include "test_touch_handler.moc"
