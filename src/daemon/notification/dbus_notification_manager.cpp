#include "dbus_notification_manager.h"
#include "../logging_categories.h"
#include "dbus/dbus_connection_helper.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

static const QString NOTIFICATIONS_SERVICE = QStringLiteral("org.freedesktop.Notifications");
static const QString NOTIFICATIONS_PATH = QStringLiteral("/org/freedesktop/Notifications");
static const QString NOTIFICATIONS_INTERFACE = QStringLiteral("org.freedesktop.Notifications");

DBusNotificationManager::DBusNotificationManager(QObject* parent)
    : QObject(parent)
{
    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Creating DBus interface";

    m_interface = std::make_unique<QDBusInterface>(
        NOTIFICATIONS_SERVICE,
        NOTIFICATIONS_PATH,
        NOTIFICATIONS_INTERFACE,
        QDBusConnection::sessionBus()
    );

    if (!m_interface->isValid()) {
        qCWarning(DBusNotificationLog) << "DBusNotificationManager: Failed to create DBus interface:"
                   << m_interface->lastError().message();
        return;
    }

    qCDebug(DBusNotificationLog) << "DBusNotificationManager: DBus interface created successfully";

    // Connect to DBus signals for action invocation and notification closing
    int const connected = DBusConnectionHelper::connectSignals(
        NOTIFICATIONS_SERVICE,
        NOTIFICATIONS_PATH,
        NOTIFICATIONS_INTERFACE,
        this,
        {
            {"ActionInvoked", SLOT(onActionInvoked(uint, QString))},
            {"NotificationClosed", SLOT(onNotificationClosed(uint, uint))}
        }
    );

    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Connected" << connected << "of 2 signals";
}

DBusNotificationManager::~DBusNotificationManager() = default;

uint DBusNotificationManager::showNotification(
    const QString& appName,
    uint replacesId,
    const QString& appIcon,
    const QString& summary,
    const QString& body,
    const QStringList& actions,
    const QVariantMap& hints,
    int expireTimeout
) {
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(DBusNotificationLog) << "DBusNotificationManager: Cannot show notification - interface not valid";
        return 0;
    }

    // Store for later use in updateNotification
    m_lastAppName = appName;
    m_lastAppIcon = appIcon;
    m_lastActions = actions;

    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Showing notification"
             << "replacesId:" << replacesId
             << "summary:" << summary
             << "body:" << body
             << "body length:" << body.length()
             << "actions:" << actions
             << "hints:" << hints
             << "timeout:" << expireTimeout;

    QDBusReply<uint> reply = m_interface->call(
        QStringLiteral("Notify"),
        appName,
        replacesId,
        appIcon,
        summary,
        body,
        actions,
        hints,
        expireTimeout
    );

    if (!reply.isValid()) {
        qCWarning(DBusNotificationLog) << "DBusNotificationManager: Failed to show notification:"
                   << reply.error().message();
        return 0;
    }

    uint const notificationId = reply.value();
    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Notification shown with ID:" << notificationId;
    return notificationId;
}

uint DBusNotificationManager::updateNotification(
    uint notificationId,
    const QString& summary,
    const QString& body,
    const QVariantMap& hints,
    int expireTimeout
) {
    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Updating notification ID:" << notificationId;

    // Preserve the last actions when updating
    return showNotification(
        m_lastAppName,
        notificationId,  // replaces_id
        m_lastAppIcon,
        summary,
        body,
        m_lastActions,  // Preserve actions
        hints,
        expireTimeout
    );
}

void DBusNotificationManager::closeNotification(uint notificationId) {
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(DBusNotificationLog) << "DBusNotificationManager: Cannot close notification - interface not valid";
        return;
    }

    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Closing notification ID:" << notificationId;

    m_interface->call(
        QStringLiteral("CloseNotification"),
        notificationId
    );
}

bool DBusNotificationManager::isAvailable() const {
    if (!m_interface || !m_interface->isValid()) {
        return false;
    }

    // Check if the service is registered
    QDBusConnectionInterface const* iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(NOTIFICATIONS_SERVICE);
}

void DBusNotificationManager::onActionInvoked(uint id, const QString& actionKey) {
    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Action invoked - ID:" << id << "action:" << actionKey;
    Q_EMIT actionInvoked(id, actionKey);
}

void DBusNotificationManager::onNotificationClosed(uint id, uint reason) {
    qCDebug(DBusNotificationLog) << "DBusNotificationManager: Notification closed - ID:" << id << "reason:" << reason;
    Q_EMIT notificationClosed(id, reason);
}
} // namespace Daemon
} // namespace YubiKeyOath
