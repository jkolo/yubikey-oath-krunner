#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QDBusInterface>
#include <QDBusReply>
#include <memory>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manager for DBus notifications using org.freedesktop.Notifications
 *
 * This class provides a wrapper around the freedesktop.org DBus notification API,
 * allowing for creation, updating, and closing of notifications with full control
 * over hints, actions, and timeouts.
 */
class DBusNotificationManager : public QObject {
    Q_OBJECT

public:
    explicit DBusNotificationManager(QObject* parent = nullptr);
    ~DBusNotificationManager() override;

    /**
     * @brief Show a new notification or update an existing one
     *
     * @param appName Application name
     * @param replacesId ID of notification to replace (0 for new notification)
     * @param appIcon Icon name or path
     * @param summary Notification title/summary
     * @param body Notification body text (supports HTML markup if server has body-markup capability)
     * @param actions List of action identifiers and labels (e.g., ["action1", "Label 1", "action2", "Label 2"])
     * @param hints Map of hints (e.g., {"urgency": 2, "value": 50})
     * @param expireTimeout Timeout in milliseconds (-1 for server default, 0 for never)
     * @return Notification ID (0 on error)
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
    );

    /**
     * @brief Update an existing notification
     *
     * This is a convenience method that calls showNotification with a non-zero replacesId
     */
    uint updateNotification(
        uint notificationId,
        const QString& summary,
        const QString& body,
        const QVariantMap& hints = QVariantMap(),
        int expireTimeout = -1
    );

    /**
     * @brief Close a notification
     *
     * @param notificationId ID of the notification to close
     */
    void closeNotification(uint notificationId);

    /**
     * @brief Check if the notification service is available
     *
     * @return true if org.freedesktop.Notifications is available
     */
    bool isAvailable() const;

Q_SIGNALS:
    /**
     * @brief Emitted when a notification action is invoked
     *
     * @param notificationId ID of the notification
     * @param actionKey The action identifier that was invoked
     */
    void actionInvoked(uint notificationId, const QString& actionKey);

    /**
     * @brief Emitted when a notification is closed
     *
     * @param notificationId ID of the notification
     * @param reason Close reason (1=expired, 2=dismissed, 3=closed, 4=undefined)
     */
    void notificationClosed(uint notificationId, uint reason);

private Q_SLOTS:
    void onActionInvoked(uint id, const QString& actionKey);
    void onNotificationClosed(uint id, uint reason);

private:
    std::unique_ptr<QDBusInterface> m_interface;
    QString m_lastAppName;
    QString m_lastAppIcon;
    QStringList m_lastActions;
};
} // namespace Daemon
} // namespace YubiKeyOath
