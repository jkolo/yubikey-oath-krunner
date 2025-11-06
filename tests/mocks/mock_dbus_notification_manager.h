/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/notification/dbus_notification_manager.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of DBusNotificationManager for testing
 *
 * Inherits from DBusNotificationManager and tracks all notification operations
 */
class MockDBusNotificationManager : public DBusNotificationManager
{
    Q_OBJECT

public:
    struct NotificationCall {
        QString method;
        uint notificationId;
        QString summary;
        QString body;
        QVariantMap hints;
        int expireTimeout;
        QStringList actions;
    };

    explicit MockDBusNotificationManager(QObject *parent = nullptr)
        : DBusNotificationManager(parent)
        , m_nextNotificationId(1)
        , m_isAvailableResult(true)
    {}

    ~MockDBusNotificationManager() override;

    /**
     * @brief Mock show notification
     */
    uint showNotification(
        const QString& appName,
        uint replacesId,
        const QString& appIcon,
        const QString& summary,
        const QString& body,
        const QStringList& actions,
        const QVariantMap& hints,
        int expireTimeout
    ) override
    {
        Q_UNUSED(appName);
        Q_UNUSED(appIcon);

        uint notifId = replacesId > 0 ? replacesId : m_nextNotificationId++;

        NotificationCall call;
        call.method = QStringLiteral("showNotification");
        call.notificationId = notifId;
        call.summary = summary;
        call.body = body;
        call.hints = hints;
        call.expireTimeout = expireTimeout;
        call.actions = actions;

        m_calls.append(call);

        // Record simplified call history
        m_callHistory.append(QString("showNotification(id=%1, summary=%2)").arg(notifId).arg(summary));

        return notifId;
    }

    /**
     * @brief Mock update notification
     */
    uint updateNotification(
        uint notificationId,
        const QString& summary,
        const QString& body,
        const QVariantMap& hints = QVariantMap(),
        int expireTimeout = -1
    ) override
    {
        NotificationCall call;
        call.method = QStringLiteral("updateNotification");
        call.notificationId = notificationId;
        call.summary = summary;
        call.body = body;
        call.hints = hints;
        call.expireTimeout = expireTimeout;

        m_calls.append(call);

        // Record simplified call history
        m_callHistory.append(QString("updateNotification(id=%1, summary=%2)").arg(notificationId).arg(summary));

        return notificationId;
    }

    /**
     * @brief Mock close notification
     */
    void closeNotification(uint notificationId) override
    {
        NotificationCall call;
        call.method = QStringLiteral("closeNotification");
        call.notificationId = notificationId;

        m_calls.append(call);

        // Record simplified call history
        m_callHistory.append(QString("closeNotification(id=%1)").arg(notificationId));
    }

    /**
     * @brief Mock availability check
     */
    bool isAvailable() const override
    {
        return m_isAvailableResult;
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets return value for isAvailable()
     */
    void setAvailable(bool available)
    {
        m_isAvailableResult = available;
    }

    /**
     * @brief Sets the next notification ID to return
     */
    void setNextNotificationId(uint id)
    {
        m_nextNotificationId = id;
    }

    /**
     * @brief Manually trigger actionInvoked signal
     */
    void triggerActionInvoked(uint notificationId, const QString& actionKey)
    {
        Q_EMIT actionInvoked(notificationId, actionKey);
    }

    /**
     * @brief Manually trigger notificationClosed signal
     */
    void triggerNotificationClosed(uint notificationId, uint reason)
    {
        Q_EMIT notificationClosed(notificationId, reason);
    }

    /**
     * @brief Simulates user clicking action button
     */
    void simulateActionInvoked(uint notificationId, const QString& actionKey)
    {
        Q_EMIT actionInvoked(notificationId, actionKey);
    }

    /**
     * @brief Simulates notification being closed
     */
    void simulateNotificationClosed(uint notificationId, uint reason)
    {
        Q_EMIT notificationClosed(notificationId, reason);
    }

    /**
     * @brief Gets all notification calls
     */
    QList<NotificationCall> calls() const
    {
        return m_calls;
    }

    /**
     * @brief Gets call history (simplified strings)
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
        return m_calls.size();
    }

    /**
     * @brief Finds calls by notification ID
     */
    QList<NotificationCall> callsForNotification(uint notificationId) const
    {
        QList<NotificationCall> result;
        for (const auto& call : m_calls) {
            if (call.notificationId == notificationId) {
                result.append(call);
            }
        }
        return result;
    }

    /**
     * @brief Gets last call for specific notification ID
     */
    NotificationCall lastCallForNotification(uint notificationId) const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].notificationId == notificationId) {
                return m_calls[i];
            }
        }
        return NotificationCall();
    }

    /**
     * @brief Gets number of show notification calls
     */
    int showCallCount() const
    {
        int count = 0;
        for (const auto& call : m_calls) {
            if (call.method == QStringLiteral("showNotification")) {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Gets number of close notification calls
     */
    int closeCallCount() const
    {
        int count = 0;
        for (const auto& call : m_calls) {
            if (call.method == QStringLiteral("closeNotification")) {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Gets last notification title (summary)
     */
    QString lastTitle() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification") ||
                m_calls[i].method == QStringLiteral("updateNotification")) {
                return m_calls[i].summary;
            }
        }
        return QString();
    }

    /**
     * @brief Gets last notification body
     */
    QString lastBody() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification") ||
                m_calls[i].method == QStringLiteral("updateNotification")) {
                return m_calls[i].body;
            }
        }
        return QString();
    }

    /**
     * @brief Gets last replaces_id from show notification
     */
    uint lastReplacesId() const
    {
        // In showNotification, if replacesId > 0, we use it, otherwise generate new
        // So we need to track if the last show call reused an ID
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification") && i > 0) {
                // Check if this notification ID matches a previous one
                uint currentId = m_calls[i].notificationId;
                for (int j = i - 1; j >= 0; --j) {
                    if (m_calls[j].notificationId == currentId) {
                        return currentId;
                    }
                }
            }
        }
        return 0;
    }

    /**
     * @brief Gets last notification actions
     */
    QStringList lastActions() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification")) {
                return m_calls[i].actions;
            }
        }
        return QStringList();
    }

    /**
     * @brief Gets last notification timeout
     */
    int lastTimeout() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification") ||
                m_calls[i].method == QStringLiteral("updateNotification")) {
                return m_calls[i].expireTimeout;
            }
        }
        return -1;
    }

    /**
     * @brief Gets last notification hints
     */
    QVariantMap lastHints() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("showNotification") ||
                m_calls[i].method == QStringLiteral("updateNotification")) {
                return m_calls[i].hints;
            }
        }
        return QVariantMap();
    }

    /**
     * @brief Gets last closed notification ID
     */
    uint lastClosedId() const
    {
        for (int i = m_calls.size() - 1; i >= 0; --i) {
            if (m_calls[i].method == QStringLiteral("closeNotification")) {
                return m_calls[i].notificationId;
            }
        }
        return 0;
    }

    /**
     * @brief Clears all tracking data
     */
    void reset()
    {
        m_calls.clear();
        m_callHistory.clear();
        m_nextNotificationId = 1;
        m_isAvailableResult = true;
    }

Q_SIGNALS:
    /**
     * @brief Emitted when a notification action is invoked
     */
    void actionInvoked(uint notificationId, const QString& actionKey);

    /**
     * @brief Emitted when a notification is closed
     */
    void notificationClosed(uint notificationId, uint reason);

private:
    uint m_nextNotificationId;
    bool m_isAvailableResult;
    QList<NotificationCall> m_calls;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
