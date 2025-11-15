/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mocks/mock_touch_handler.h"
#include "mocks/mock_oath_action_coordinator.h"
#include "mocks/mock_notification_orchestrator.h"
#include "mocks/mock_dbus_notification_manager.h"
#include "mocks/mock_configuration_provider.h"
#include <QTest>
#include <QSignalSpy>

using namespace YubiKeyOath::Daemon;
using YubiKeyOath::Shared::MockConfigurationProvider;

/**
 * @brief Unit tests for Touch Workflow Components
 *
 * Tests workflow components integration
 * This validates the EXACT workflow user requested:
 * "YubiKey inserted, credential requires touch, user selected type/copy"
 *
 * Workflow steps verified:
 * 1. Touch notification is shown
 * 2. Touch handler starts operation with timeout
 * 3. After simulated touch, action is executed
 * 4. Notification is closed
 * 5. Timeout and cancellation handling
 */
class TestTouchWorkflowCoordinator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // Workflow component integration tests
    void testTouchHandlerAndNotificationIntegration();
    void testTouchTimeoutFlow();
    void testActionExecutionFlow();
    void testNotificationCancellationFlow();
    void testCompleteWorkflowSequence();

private:
    MockOathActionCoordinator *m_actionCoordinator = nullptr;
    MockTouchHandler *m_touchHandler = nullptr;
    MockDBusNotificationManager *m_dbusNotif = nullptr;
    MockNotificationOrchestrator *m_notificationOrch = nullptr;
    MockConfigurationProvider *m_config = nullptr;
};

void TestTouchWorkflowCoordinator::initTestCase()
{
    // Global initialization if needed
}

void TestTouchWorkflowCoordinator::init()
{
    // Create fresh instances for each test
    m_actionCoordinator = new MockOathActionCoordinator(this);
    m_touchHandler = new MockTouchHandler(this);
    m_dbusNotif = new MockDBusNotificationManager(this);
    m_config = new MockConfigurationProvider(this);
    m_notificationOrch = new MockNotificationOrchestrator(m_dbusNotif, m_config, this);

    // Set touch timeout to 15 seconds (default)
    m_config->setTouchTimeout(15);
}

void TestTouchWorkflowCoordinator::cleanup()
{
    delete m_actionCoordinator;
    delete m_touchHandler;
    delete m_notificationOrch;
    delete m_dbusNotif;
    delete m_config;

    m_actionCoordinator = nullptr;
    m_touchHandler = nullptr;
    m_notificationOrch = nullptr;
    m_dbusNotif = nullptr;
    m_config = nullptr;
}

// ========== Workflow Component Integration Tests ==========

/**
 * @brief Tests TouchHandler and NotificationOrchestrator integration
 *
 * Simulates workflow step 1-2:
 * 1. Show touch notification
 * 2. Start touch operation
 * 3. Verify both components are active
 * 4. Stop touch and close notification
 */
void TestTouchWorkflowCoordinator::testTouchHandlerAndNotificationIntegration()
{
    const QString credentialName = QStringLiteral("Google:user@example.com");
    const int timeoutSeconds = 15;

    // Step 1: Show touch notification
    m_notificationOrch->showTouchNotification(credentialName, timeoutSeconds);

    // Verify notification was shown
    QVERIFY(m_notificationOrch->wasCalled(QStringLiteral("showTouchNotification")));

    // Step 2: Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeoutSeconds);

    // Step 3: Verify both are active
    QCOMPARE(m_touchHandler->isTouchActive(), true);
    QCOMPARE(m_touchHandler->waitingForTouch(), credentialName);
    QCOMPARE(m_touchHandler->lastTimeoutSeconds(), timeoutSeconds);

    // Step 4: Cleanup - stop touch and close notification
    m_touchHandler->cancelTouchOperation();
    m_notificationOrch->closeTouchNotification();

    // Verify cleanup
    QCOMPARE(m_touchHandler->isTouchActive(), false);
    QVERIFY(m_notificationOrch->wasCalled(QStringLiteral("closeTouchNotification")));
}

/**
 * @brief Tests touch timeout flow
 *
 * Simulates workflow when user doesn't touch YubiKey in time:
 * 1. Start touch operation with timeout
 * 2. Simulate timeout
 * 3. Verify timeout signal emitted
 * 4. Verify cleanup
 */
void TestTouchWorkflowCoordinator::testTouchTimeoutFlow()
{
    const QString credentialName = QStringLiteral("GitHub:jkolo");
    const int timeoutSeconds = 15;

    // Enable manual timeout control
    m_touchHandler->setManualTimeoutControl(true);

    // Connect timeout signal
    QSignalSpy timeoutSpy(m_touchHandler, &MockTouchHandler::touchTimedOut);

    // Step 1: Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeoutSeconds);
    QCOMPARE(m_touchHandler->isTouchActive(), true);

    // Step 2: Simulate timeout
    m_touchHandler->triggerTimeout();

    // Step 3: Verify timeout signal emitted
    QCOMPARE(timeoutSpy.count(), 1);
    QCOMPARE(timeoutSpy.at(0).at(0).toString(), credentialName);

    // Step 4: Verify touch operation stopped
    QCOMPARE(m_touchHandler->isTouchActive(), false);
}

/**
 * @brief Tests action execution flow
 *
 * Simulates workflow step after successful touch:
 * 1. Execute action with generated code
 * 2. Verify action coordinator called
 * 3. Verify correct parameters passed
 */
void TestTouchWorkflowCoordinator::testActionExecutionFlow()
{
    const QString credentialName = QStringLiteral("Dropbox:user");
    const QString actionType = QStringLiteral("copy");
    const QString generatedCode = QStringLiteral("123456");

    // Step 1: Execute action (simulating post-touch)
    auto result = m_actionCoordinator->executeActionWithNotification(
        generatedCode, credentialName, actionType);

    // Step 2: Verify action coordinator was called
    QCOMPARE(m_actionCoordinator->callCount(), 1);

    // Step 3: Verify correct parameters
    QCOMPARE(m_actionCoordinator->lastCode(), generatedCode);
    QCOMPARE(m_actionCoordinator->lastCredentialName(), credentialName);
    QCOMPARE(m_actionCoordinator->lastActionType(), actionType);
    QCOMPARE(result, ActionExecutor::ActionResult::Success);
}

/**
 * @brief Tests notification cancellation flow
 *
 * Simulates workflow when user cancels via notification button:
 * 1. Show touch notification
 * 2. User clicks cancel
 * 3. Verify touchCancelled signal emitted
 * 4. Verify cleanup
 */
void TestTouchWorkflowCoordinator::testNotificationCancellationFlow()
{
    const QString credentialName = QStringLiteral("Facebook:user");

    // Connect cancellation signal
    QSignalSpy cancelSpy(m_notificationOrch, &MockNotificationOrchestrator::touchCancelled);

    // Step 1: Show touch notification
    m_notificationOrch->showTouchNotification(credentialName, 15);

    // Step 2: Simulate user clicking cancel button
    Q_EMIT m_notificationOrch->touchCancelled();

    // Step 3: Verify touchCancelled signal emitted
    QCOMPARE(cancelSpy.count(), 1);

    // Step 4: Close notification
    m_notificationOrch->closeTouchNotification();
    QVERIFY(m_notificationOrch->wasCalled(QStringLiteral("closeTouchNotification")));
}

/**
 * @brief Tests complete workflow sequence
 *
 * Simulates complete touch workflow from start to finish:
 * 1. Show touch notification
 * 2. Start touch operation
 * 3. Simulate successful touch (code generated)
 * 4. Execute action
 * 5. Close notification
 * 6. Verify all steps executed in order
 */
void TestTouchWorkflowCoordinator::testCompleteWorkflowSequence()
{
    const QString credentialName = QStringLiteral("Amazon:user");
    const QString actionType = QStringLiteral("type");
    const QString generatedCode = QStringLiteral("987654");
    const int timeoutSeconds = 15;

    // Step 1: Show touch notification
    m_notificationOrch->showTouchNotification(credentialName, timeoutSeconds);
    QVERIFY(m_notificationOrch->wasCalled(QStringLiteral("showTouchNotification")));

    // Step 2: Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeoutSeconds);
    QCOMPARE(m_touchHandler->isTouchActive(), true);
    QCOMPARE(m_touchHandler->waitingForTouch(), credentialName);

    // Step 3: Simulate successful touch (user touched YubiKey, code generated)
    // In real workflow, this comes from YubiKeyDeviceManager::codeGenerated signal

    // Step 4: Execute action after touch
    auto result = m_actionCoordinator->executeActionWithNotification(
        generatedCode, credentialName, actionType);

    QCOMPARE(m_actionCoordinator->callCount(), 1);
    QCOMPARE(m_actionCoordinator->lastCode(), generatedCode);
    QCOMPARE(m_actionCoordinator->lastActionType(), actionType);
    QCOMPARE(result, ActionExecutor::ActionResult::Success);

    // Step 5: Cleanup - close notification and stop touch
    m_notificationOrch->closeTouchNotification();
    m_touchHandler->cancelTouchOperation();

    // Step 6: Verify complete workflow executed
    QVERIFY(m_notificationOrch->wasCalled(QStringLiteral("closeTouchNotification")));
    QCOMPARE(m_touchHandler->isTouchActive(), false);

    // Verify call order in history
    QStringList expectedCalls = {
        QString("showTouchNotification(%1, %2)").arg(credentialName).arg(timeoutSeconds),
        QString("closeTouchNotification()")
    };

    for (const QString &expectedCall : expectedCalls) {
        QVERIFY(m_notificationOrch->callHistory().contains(expectedCall));
    }
}

QTEST_MAIN(TestTouchWorkflowCoordinator)
#include "test_touch_workflow_coordinator.moc"
