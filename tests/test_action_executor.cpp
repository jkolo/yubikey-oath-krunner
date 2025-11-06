/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemon/actions/action_executor.h"
#include "mocks/mock_text_input_provider.h"
#include "mocks/mock_clipboard_manager.h"
#include "mocks/mock_notification_orchestrator.h"
#include "mocks/mock_dbus_notification_manager.h"
#include "mocks/mock_configuration_provider.h"
#include <QTest>

using namespace YubiKeyOath::Daemon;
using YubiKeyOath::Shared::MockConfigurationProvider;

/**
 * @brief Unit tests for ActionExecutor
 *
 * Tests action execution with fallback strategies
 */
class TestActionExecutor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // Copy action tests
    void testExecuteCopyAction_Success();
    void testExecuteCopyAction_Failure();

    // Type action tests
    void testExecuteTypeAction_Success();
    void testExecuteTypeAction_FallbackToClipboard();
    void testExecuteTypeAction_WaitingForPermission();
    void testExecuteTypeAction_BothFailed();

private:
    MockTextInputProvider *m_textInput = nullptr;
    MockClipboardManager *m_clipboard = nullptr;
    MockDBusNotificationManager *m_dbusNotif = nullptr;
    MockNotificationOrchestrator *m_notificationOrch = nullptr;
    MockConfigurationProvider *m_config = nullptr;
    ActionExecutor *m_executor = nullptr;
};

void TestActionExecutor::initTestCase()
{
    // Global initialization if needed
}

void TestActionExecutor::init()
{
    // Create fresh instances for each test
    m_textInput = new MockTextInputProvider(this);
    m_clipboard = new MockClipboardManager(this);
    m_dbusNotif = new MockDBusNotificationManager(this);
    m_config = new MockConfigurationProvider(this);
    m_notificationOrch = new MockNotificationOrchestrator(m_dbusNotif, m_config, this);

    m_executor = new ActionExecutor(
        m_textInput,
        m_clipboard,
        m_config,
        m_notificationOrch,
        this
    );
}

void TestActionExecutor::cleanup()
{
    delete m_executor;
    m_executor = nullptr;

    delete m_textInput;
    delete m_clipboard;
    delete m_notificationOrch;
    delete m_dbusNotif;
    delete m_config;

    m_textInput = nullptr;
    m_clipboard = nullptr;
    m_notificationOrch = nullptr;
    m_dbusNotif = nullptr;
    m_config = nullptr;
}

// ========== Copy Action Tests ==========

void TestActionExecutor::testExecuteCopyAction_Success()
{
    m_clipboard->setShouldSucceed(true);

    auto result = m_executor->executeCopyAction(QStringLiteral("123456"), QStringLiteral("Google"));

    QCOMPARE(result, ActionExecutor::ActionResult::Success);
    QCOMPARE(m_clipboard->lastCopiedText(), QStringLiteral("123456"));
    QCOMPARE(m_clipboard->copiedCount(), 1);
}

void TestActionExecutor::testExecuteCopyAction_Failure()
{
    m_clipboard->setShouldSucceed(false);

    auto result = m_executor->executeCopyAction(QStringLiteral("654321"), QStringLiteral("GitHub"));

    QCOMPARE(result, ActionExecutor::ActionResult::Failed);

    // Note: ActionExecutor emits notificationRequested signal, it doesn't call
    // NotificationOrchestrator directly, so we can't verify notification in this unit test
    // (would need to connect to signal or test at higher level)
}

// ========== Type Action Tests ==========

void TestActionExecutor::testExecuteTypeAction_Success()
{
    m_textInput->setTypeTextResult(true);

    auto result = m_executor->executeTypeAction(QStringLiteral("987654"), QStringLiteral("Amazon"));

    QCOMPARE(result, ActionExecutor::ActionResult::Success);
    QCOMPARE(m_textInput->lastTypedText(), QStringLiteral("987654"));
    QCOMPARE(m_textInput->typeTextCallCount(), 1);

    // Should not have used clipboard
    QCOMPARE(m_clipboard->copiedCount(), 0);
}

void TestActionExecutor::testExecuteTypeAction_FallbackToClipboard()
{
    // Type fails, but not due to permission
    m_textInput->setTypeTextResult(false);
    m_textInput->setWaitingForPermission(false);
    m_textInput->setPermissionRejected(false);

    m_clipboard->setShouldSucceed(true);

    auto result = m_executor->executeTypeAction(QStringLiteral("111222"), QStringLiteral("Facebook"));

    // Fallback still returns Failed (even though clipboard succeeded)
    QCOMPARE(result, ActionExecutor::ActionResult::Failed);
    QCOMPARE(m_clipboard->lastCopiedText(), QStringLiteral("111222"));
    QCOMPARE(m_clipboard->copiedCount(), 1);

    // Note: ActionExecutor uses Q_EMIT notificationRequested, not NotificationOrchestrator
    // So we can't verify notification calls in this test
}

void TestActionExecutor::testExecuteTypeAction_WaitingForPermission()
{
    m_textInput->setTypeTextResult(false);
    m_textInput->setWaitingForPermission(true);

    auto result = m_executor->executeTypeAction(QStringLiteral("333444"), QStringLiteral("Dropbox"));

    QCOMPARE(result, ActionExecutor::ActionResult::WaitingForPermission);

    // Should not have used clipboard (waiting for user)
    QCOMPARE(m_clipboard->copiedCount(), 0);
}

void TestActionExecutor::testExecuteTypeAction_BothFailed()
{
    // Both type and clipboard fail
    m_textInput->setTypeTextResult(false);
    m_textInput->setWaitingForPermission(false);
    m_textInput->setPermissionRejected(false);

    m_clipboard->setShouldSucceed(false);

    auto result = m_executor->executeTypeAction(QStringLiteral("555666"), QStringLiteral("Twitter"));

    // Should return Failed when both methods fail
    QCOMPARE(result, ActionExecutor::ActionResult::Failed);

    // Both type and clipboard were attempted
    QCOMPARE(m_textInput->typeTextCallCount(), 1);
    QCOMPARE(m_clipboard->copiedCount(), 1);  // Fallback attempted even though it failed
}

QTEST_MAIN(TestActionExecutor)
#include "test_action_executor.moc"
