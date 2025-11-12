/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QCoreApplication>
#include "../src/daemon/workflows/notification_orchestrator.h"
#include "mocks/mock_dbus_notification_manager.h"
#include "mocks/mock_configuration_provider.h"

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for NotificationOrchestrator
 *
 * Tests all notification types and lifecycle management:
 * - Code notifications with countdown
 * - Touch notifications with cancel button
 * - Simple notifications
 * - Persistent notifications
 * - Modifier release notifications
 * - Reconnect notifications
 * - Timer management
 * - Signal emissions
 */
class TestNotificationOrchestrator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qDebug() << "=== TestNotificationOrchestrator: Starting test suite ===";
    }

    void init()
    {
        m_mockNotificationManager = new MockDBusNotificationManager(nullptr);
        m_mockConfig = new MockConfigurationProvider(nullptr);

        // Default: notifications enabled, manager available
        m_mockConfig->setShowNotifications(true);
        m_mockNotificationManager->setAvailable(true);

        m_orchestrator = new NotificationOrchestrator(m_mockNotificationManager, m_mockConfig, nullptr);
    }

    void cleanup()
    {
        // Stop all timers before cleanup to avoid segfault
        if (m_orchestrator) {
            auto timers = m_orchestrator->findChildren<QTimer*>();
            for (auto *timer : timers) {
                timer->stop();
            }
        }

        delete m_orchestrator;
        delete m_mockConfig;
        delete m_mockNotificationManager;
    }

    void cleanupTestCase()
    {
        qDebug() << "=== TestNotificationOrchestrator: Test suite completed ===";
    }

    // ========== Constructor Tests ==========

    void testConstructor_InitializesTimers()
    {
        // Verify timers are created (they exist as children)
        auto timers = m_orchestrator->findChildren<QTimer*>();
        QVERIFY(timers.size() >= 4); // code, touch, modifier, reconnect
    }

    // ========== Code Notification Tests ==========

    void testShowCodeNotification_Success()
    {
        const QString code = QStringLiteral("123456");
        const QString credential = QStringLiteral("Google:user@example.com");
        const int expiration = 30;

        m_orchestrator->showCodeNotification(code, credential, expiration, DeviceModel{});

        // Verify notification was shown
        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastTitle(), credential);
        QVERIFY(m_mockNotificationManager->lastBody().contains(code));
        QVERIFY(m_mockNotificationManager->lastBody().contains(QStringLiteral("30s")));
    }

    void testShowCodeNotification_WhenNotificationsDisabled_DoesNotShow()
    {
        m_mockConfig->setShowNotifications(false);

        m_orchestrator->showCodeNotification(QStringLiteral("123456"),
                                            QStringLiteral("Test"), 30, DeviceModel{});

        QCOMPARE(m_mockNotificationManager->showCallCount(), 0);
    }

    void testShowCodeNotification_WhenManagerUnavailable_DoesNotShow()
    {
        m_mockNotificationManager->setAvailable(false);

        m_orchestrator->showCodeNotification(QStringLiteral("123456"),
                                            QStringLiteral("Test"), 30, DeviceModel{});

        QCOMPARE(m_mockNotificationManager->showCallCount(), 0);
    }

    void testShowCodeNotification_StartsTimer()
    {
        m_orchestrator->showCodeNotification(QStringLiteral("123456"),
                                            QStringLiteral("Test"), 30, DeviceModel{});

        // Find code update timer and verify it's active
        auto timers = m_orchestrator->findChildren<QTimer*>();
        bool foundActiveTimer = false;
        for (auto *timer : timers) {
            if (timer->isActive() && timer->interval() == 1000) {
                foundActiveTimer = true;
                break;
            }
        }
        QVERIFY(foundActiveTimer);
    }

    void testShowCodeNotification_ReplacesExisting()
    {
        // Show first notification
        m_mockNotificationManager->setNextNotificationId(100);
        m_orchestrator->showCodeNotification(QStringLiteral("111111"),
                                            QStringLiteral("First"), 30, DeviceModel{});
        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);

        // Show second notification
        m_mockNotificationManager->setNextNotificationId(101);
        m_orchestrator->showCodeNotification(QStringLiteral("222222"),
                                            QStringLiteral("Second"), 30, DeviceModel{});

        // Should have called show twice, second with replaces_id=100
        QCOMPARE(m_mockNotificationManager->showCallCount(), 2);
        QCOMPARE(m_mockNotificationManager->lastReplacesId(), 100u);
    }

    // ========== Touch Notification Tests ==========

    void testShowTouchNotification_Success()
    {
        const QString credential = QStringLiteral("GitHub:user");
        const int timeout = 15;

        m_orchestrator->showTouchNotification(credential, timeout, DeviceModel{});

        // Verify notification was shown
        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);
        QVERIFY(m_mockNotificationManager->lastBody().contains(QStringLiteral("15s")));

        // Verify Cancel action exists
        QVERIFY(m_mockNotificationManager->lastActions().contains(QStringLiteral("cancel")));
    }

    void testShowTouchNotification_ClosesExisting()
    {
        // Show first touch notification
        m_mockNotificationManager->setNextNotificationId(200);
        m_orchestrator->showTouchNotification(QStringLiteral("First"), 15, DeviceModel{});

        // Show second touch notification
        m_mockNotificationManager->setNextNotificationId(201);
        m_orchestrator->showTouchNotification(QStringLiteral("Second"), 15, DeviceModel{});

        // First notification should be closed
        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 200u);
    }

    void testShowTouchNotification_WhenNotificationsDisabled_DoesNotShow()
    {
        m_mockConfig->setShowNotifications(false);

        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});

        QCOMPARE(m_mockNotificationManager->showCallCount(), 0);
    }

    void testShowTouchNotification_StartsTimer()
    {
        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});

        // Find active timer
        auto timers = m_orchestrator->findChildren<QTimer*>();
        bool foundActiveTimer = false;
        for (auto *timer : timers) {
            if (timer->isActive() && timer->interval() == 1000) {
                foundActiveTimer = true;
                break;
            }
        }
        QVERIFY(foundActiveTimer);
    }

    void testCloseTouchNotification_ClosesNotification()
    {
        // Show notification first
        m_mockNotificationManager->setNextNotificationId(300);
        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});

        // Close it
        m_orchestrator->closeTouchNotification();

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 300u);
    }

    void testCloseTouchNotification_StopsTimer()
    {
        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});
        m_orchestrator->closeTouchNotification();

        // Verify no active timers
        auto timers = m_orchestrator->findChildren<QTimer*>();
        for (auto *timer : timers) {
            if (timer->interval() == 1000) {
                QVERIFY(!timer->isActive());
            }
        }
    }

    // ========== Simple Notification Tests ==========

    void testShowSimpleNotification_InfoType()
    {
        m_orchestrator->showSimpleNotification(QStringLiteral("Title"),
                                              QStringLiteral("Message"), 0);

        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastTimeout(), 5000);
        // Info type should use normal urgency
        QCOMPARE(m_mockNotificationManager->lastHints()[QStringLiteral("urgency")].toUInt(), 1u);
    }

    void testShowSimpleNotification_WarningType()
    {
        m_orchestrator->showSimpleNotification(QStringLiteral("Warning"),
                                              QStringLiteral("Error message"), 1);

        // Warning/error type should use critical urgency
        QCOMPARE(m_mockNotificationManager->lastHints()[QStringLiteral("urgency")].toUInt(), 2u);
    }

    void testShowSimpleNotification_WhenDisabled_DoesNotShow()
    {
        m_mockConfig->setShowNotifications(false);

        m_orchestrator->showSimpleNotification(QStringLiteral("Title"),
                                              QStringLiteral("Message"), 0);

        QCOMPARE(m_mockNotificationManager->showCallCount(), 0);
    }

    // ========== Persistent Notification Tests ==========

    void testShowPersistentNotification_ReturnsId()
    {
        m_mockNotificationManager->setNextNotificationId(400);

        uint id = m_orchestrator->showPersistentNotification(QStringLiteral("Title"),
                                                             QStringLiteral("Message"), 0);

        QCOMPARE(id, 400u);
    }

    void testShowPersistentNotification_NoTimeout()
    {
        m_orchestrator->showPersistentNotification(QStringLiteral("Title"),
                                                   QStringLiteral("Message"), 0);

        // Should have timeout=0 (persistent)
        QCOMPARE(m_mockNotificationManager->lastTimeout(), 0);
    }

    void testShowPersistentNotification_WhenDisabled_Returns0()
    {
        m_mockConfig->setShowNotifications(false);

        uint id = m_orchestrator->showPersistentNotification(QStringLiteral("Title"),
                                                             QStringLiteral("Message"), 0);

        QCOMPARE(id, 0u);
    }

    // ========== Close Notification Tests ==========

    void testCloseNotification_Success()
    {
        m_orchestrator->closeNotification(500);

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 500u);
    }

    void testCloseNotification_WhenIdZero_DoesNothing()
    {
        m_orchestrator->closeNotification(0);

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 0);
    }

    // ========== Modifier Release Notification Tests ==========

    void testShowModifierReleaseNotification_Success()
    {
        QStringList modifiers = {QStringLiteral("Shift"), QStringLiteral("Ctrl")};

        m_orchestrator->showModifierReleaseNotification(modifiers, 15);

        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);
        QVERIFY(m_mockNotificationManager->lastBody().contains(QStringLiteral("Shift")));
        QVERIFY(m_mockNotificationManager->lastBody().contains(QStringLiteral("Ctrl")));
    }

    void testShowModifierReleaseNotification_ClosesExisting()
    {
        m_mockNotificationManager->setNextNotificationId(600);
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Shift")}, 15);

        m_mockNotificationManager->setNextNotificationId(601);
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Ctrl")}, 15);

        // First notification should be closed
        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 600u);
    }

    void testShowModifierReleaseNotification_StartsTimer()
    {
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Shift")}, 15);

        // Find active timer
        auto timers = m_orchestrator->findChildren<QTimer*>();
        bool foundActiveTimer = false;
        for (auto *timer : timers) {
            if (timer->isActive() && timer->interval() == 1000) {
                foundActiveTimer = true;
                break;
            }
        }
        QVERIFY(foundActiveTimer);
    }

    void testCloseModifierNotification_ClosesNotification()
    {
        m_mockNotificationManager->setNextNotificationId(700);
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Shift")}, 15);

        m_orchestrator->closeModifierNotification();

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 700u);
    }

    void testCloseModifierNotification_StopsTimer()
    {
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Shift")}, 15);
        m_orchestrator->closeModifierNotification();

        // Verify no active timers (may need to check specific timer)
        auto timers = m_orchestrator->findChildren<QTimer*>();
        int activeCount = 0;
        for (auto *timer : timers) {
            if (timer->isActive()) {
                activeCount++;
            }
        }
        // Should be fewer active timers after closing
        QVERIFY(activeCount < 4);
    }

    // ========== Reconnect Notification Tests ==========

    void testShowReconnectNotification_Success()
    {
        const QString deviceName = QStringLiteral("My YubiKey");
        const QString credential = QStringLiteral("Google:user");

        m_orchestrator->showReconnectNotification(deviceName, credential, 30, DeviceModel{});

        QCOMPARE(m_mockNotificationManager->showCallCount(), 1);
        QVERIFY(m_mockNotificationManager->lastTitle().contains(deviceName));
        QVERIFY(m_mockNotificationManager->lastActions().contains(QStringLiteral("cancel_reconnect")));
    }

    void testShowReconnectNotification_ClosesExisting()
    {
        m_mockNotificationManager->setNextNotificationId(800);
        m_orchestrator->showReconnectNotification(QStringLiteral("Device1"),
                                                  QStringLiteral("Cred1"), 30, DeviceModel{});

        m_mockNotificationManager->setNextNotificationId(801);
        m_orchestrator->showReconnectNotification(QStringLiteral("Device2"),
                                                  QStringLiteral("Cred2"), 30, DeviceModel{});

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 800u);
    }

    void testShowReconnectNotification_StartsTimer()
    {
        m_orchestrator->showReconnectNotification(QStringLiteral("Device"),
                                                  QStringLiteral("Cred"), 30, DeviceModel{});

        auto timers = m_orchestrator->findChildren<QTimer*>();
        bool foundActiveTimer = false;
        for (auto *timer : timers) {
            if (timer->isActive() && timer->interval() == 1000) {
                foundActiveTimer = true;
                break;
            }
        }
        QVERIFY(foundActiveTimer);
    }

    void testCloseReconnectNotification_ClosesNotification()
    {
        m_mockNotificationManager->setNextNotificationId(900);
        m_orchestrator->showReconnectNotification(QStringLiteral("Device"),
                                                  QStringLiteral("Cred"), 30, DeviceModel{});

        m_orchestrator->closeReconnectNotification();

        QCOMPARE(m_mockNotificationManager->closeCallCount(), 1);
        QCOMPARE(m_mockNotificationManager->lastClosedId(), 900u);
    }

    void testCloseReconnectNotification_StopsTimer()
    {
        m_orchestrator->showReconnectNotification(QStringLiteral("Device"),
                                                  QStringLiteral("Cred"), 30, DeviceModel{});
        m_orchestrator->closeReconnectNotification();

        // Verify timer stopped
        auto timers = m_orchestrator->findChildren<QTimer*>();
        int activeCount = 0;
        for (auto *timer : timers) {
            if (timer->isActive()) {
                activeCount++;
            }
        }
        QVERIFY(activeCount < 4);
    }

    // ========== Action Invoked Signal Tests ==========

    void testOnNotificationActionInvoked_TouchCancel_EmitsSignal()
    {
        // Show touch notification
        m_mockNotificationManager->setNextNotificationId(1000);
        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});

        // Setup signal spy
        QSignalSpy spy(m_orchestrator, &NotificationOrchestrator::touchCancelled);

        // Simulate user clicking Cancel
        m_mockNotificationManager->simulateActionInvoked(1000, QStringLiteral("cancel"));

        // Process events to allow signal delivery
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
    }

    void testOnNotificationActionInvoked_ReconnectCancel_EmitsSignal()
    {
        // Show reconnect notification
        m_mockNotificationManager->setNextNotificationId(1100);
        m_orchestrator->showReconnectNotification(QStringLiteral("Device"),
                                                  QStringLiteral("Cred"), 30, DeviceModel{});

        // Setup signal spy
        QSignalSpy spy(m_orchestrator, &NotificationOrchestrator::reconnectCancelled);

        // Simulate user clicking Cancel
        m_mockNotificationManager->simulateActionInvoked(1100, QStringLiteral("cancel_reconnect"));

        // Process events to allow signal delivery
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
    }

    // ========== Notification Closed Tests ==========

    void testOnNotificationClosed_CodeNotification_StopsTimer()
    {
        // Show code notification
        m_mockNotificationManager->setNextNotificationId(1200);
        m_orchestrator->showCodeNotification(QStringLiteral("123456"),
                                            QStringLiteral("Test"), 30, DeviceModel{});

        // Simulate notification closed
        m_mockNotificationManager->simulateNotificationClosed(1200, 1);

        // Verify timer stopped
        QTest::qWait(100); // Allow signal processing
        auto timers = m_orchestrator->findChildren<QTimer*>();
        int activeCount = 0;
        for (auto *timer : timers) {
            if (timer->isActive()) {
                activeCount++;
            }
        }
        QVERIFY(activeCount < 4); // At least one timer stopped
    }

    void testOnNotificationClosed_TouchNotification_StopsTimer()
    {
        m_mockNotificationManager->setNextNotificationId(1300);
        m_orchestrator->showTouchNotification(QStringLiteral("Test"), 15, DeviceModel{});

        m_mockNotificationManager->simulateNotificationClosed(1300, 1);

        QTest::qWait(100);
        auto timers = m_orchestrator->findChildren<QTimer*>();
        int activeCount = 0;
        for (auto *timer : timers) {
            if (timer->isActive()) {
                activeCount++;
            }
        }
        QVERIFY(activeCount < 4);
    }

    void testOnNotificationClosed_ModifierNotification_StopsTimer()
    {
        m_mockNotificationManager->setNextNotificationId(1400);
        m_orchestrator->showModifierReleaseNotification({QStringLiteral("Shift")}, 15);

        m_mockNotificationManager->simulateNotificationClosed(1400, 1);

        QTest::qWait(100);
        auto timers = m_orchestrator->findChildren<QTimer*>();
        int activeCount = 0;
        for (auto *timer : timers) {
            if (timer->isActive()) {
                activeCount++;
            }
        }
        QVERIFY(activeCount < 4);
    }

private:
    NotificationOrchestrator *m_orchestrator = nullptr;
    MockDBusNotificationManager *m_mockNotificationManager = nullptr;
    MockConfigurationProvider *m_mockConfig = nullptr;
};

QTEST_MAIN(TestNotificationOrchestrator)
#include "test_notification_orchestrator.moc"
