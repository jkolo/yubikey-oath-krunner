/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/workflows/notification_orchestrator.h"
#include <QObject>
#include <QString>
#include <QStringList>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of NotificationOrchestrator for testing
 *
 * Inherits from NotificationOrchestrator and tracks calls without showing actual notifications
 */
class MockNotificationOrchestrator : public NotificationOrchestrator
{
    Q_OBJECT

public:
    explicit MockNotificationOrchestrator(DBusNotificationManager *notificationManager,
                                         const ConfigurationProvider *config,
                                         QObject *parent = nullptr)
        : NotificationOrchestrator(notificationManager, config, parent)
        , m_nextNotificationId(1)
    {}

    ~MockNotificationOrchestrator() override;

    // ========== Notification Methods (Override) ==========

    void showCodeNotification(const QString &code,
                             const QString &credentialName,
                             int expirationSeconds)
    {
        m_callHistory.append(QString("showCodeNotification(%1, %2, %3)")
            .arg(code, credentialName).arg(expirationSeconds));
        // Don't call base class
    }

    void showTouchNotification(const QString &credentialName, int timeoutSeconds)
    {
        m_callHistory.append(QString("showTouchNotification(%1, %2)")
            .arg(credentialName).arg(timeoutSeconds));
        // Don't call base class
    }

    void closeTouchNotification()
    {
        m_callHistory.append(QStringLiteral("closeTouchNotification()"));
        // Don't call base class
    }

    void showSimpleNotification(const QString &title, const QString &message, int type = 0)
    {
        m_callHistory.append(QString("showSimpleNotification(%1, %2, %3)")
            .arg(title, message).arg(type));
        // Don't call base class
    }

    uint showPersistentNotification(const QString &title, const QString &message, int type = 0)
    {
        uint id = m_nextNotificationId++;
        m_callHistory.append(QString("showPersistentNotification(%1, %2, %3) -> %4")
            .arg(title, message).arg(type).arg(id));
        // Don't call base class
        return id;
    }

    void closeNotification(uint notificationId)
    {
        m_callHistory.append(QString("closeNotification(%1)").arg(notificationId));
        // Don't call base class
    }

    void showModifierReleaseNotification(const QStringList& modifiers, int timeoutSeconds)
    {
        m_callHistory.append(QString("showModifierReleaseNotification([%1], %2)")
            .arg(modifiers.join(", ")).arg(timeoutSeconds));
        // Don't call base class
    }

    void closeModifierNotification()
    {
        m_callHistory.append(QStringLiteral("closeModifierNotification()"));
        // Don't call base class
    }

    void showModifierCancelNotification()
    {
        m_callHistory.append(QStringLiteral("showModifierCancelNotification()"));
        // Don't call base class
    }

    void showReconnectNotification(const QString &deviceName,
                                   const QString &credentialName,
                                   int timeoutSeconds)
    {
        m_callHistory.append(QString("showReconnectNotification(%1, %2, %3)")
            .arg(deviceName, credentialName).arg(timeoutSeconds));
        // Don't call base class
    }

    void closeReconnectNotification()
    {
        m_callHistory.append(QStringLiteral("closeReconnectNotification()"));
        // Don't call base class
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Gets call history for verification
     * @return List of method calls with arguments
     */
    QStringList callHistory() const
    {
        return m_callHistory;
    }

    /**
     * @brief Gets number of calls
     */
    int callCount() const
    {
        return m_callHistory.size();
    }

    /**
     * @brief Checks if specific method was called
     */
    bool wasCalled(const QString &methodPattern) const
    {
        for (const QString &call : m_callHistory) {
            if (call.contains(methodPattern)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Counts calls matching pattern
     */
    int countCalls(const QString &methodPattern) const
    {
        int count = 0;
        for (const QString &call : m_callHistory) {
            if (call.contains(methodPattern)) {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Manually triggers touchCancelled signal
     */
    void triggerTouchCancelled()
    {
        Q_EMIT touchCancelled();
    }

    /**
     * @brief Manually triggers reconnectCancelled signal
     */
    void triggerReconnectCancelled()
    {
        Q_EMIT reconnectCancelled();
    }

    /**
     * @brief Simulates user cancelling reconnect notification
     */
    void simulateReconnectCancelled()
    {
        m_callHistory.append(QStringLiteral("simulateReconnectCancelled()"));
        Q_EMIT reconnectCancelled();
    }

    /**
     * @brief Clears all tracking data
     */
    void reset()
    {
        m_callHistory.clear();
        m_nextNotificationId = 1;
    }

private:
    uint m_nextNotificationId;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
