/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "portal_permission_manager.h"
#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

PortalPermissionManager::PortalPermissionManager(QObject *parent)
    : QObject(parent)
{
    // Create D-Bus interface to Permission Store
    m_permissionStore = std::make_unique<QDBusInterface>(
        QString::fromLatin1(PERMISSION_STORE_SERVICE),
        QString::fromLatin1(PERMISSION_STORE_PATH),
        QString::fromLatin1(PERMISSION_STORE_INTERFACE),
        QDBusConnection::sessionBus(),
        this
    );

    if (!m_permissionStore->isValid()) {
        qWarning() << "PortalPermissionManager: Failed to connect to Permission Store:"
                   << m_permissionStore->lastError().message();
    }
}

PortalPermissionManager::~PortalPermissionManager() = default;

bool PortalPermissionManager::hasScreenshotPermission() const
{
    return hasPermission(QString::fromLatin1(PERMISSION_SCREENSHOT));
}

bool PortalPermissionManager::hasRemoteDesktopPermission() const
{
    return hasPermission(QString::fromLatin1(PERMISSION_REMOTE_DESKTOP));
}

Result<void> PortalPermissionManager::setScreenshotPermission(bool enable)
{
    return setPermission(QString::fromLatin1(PERMISSION_SCREENSHOT), enable);
}

Result<void> PortalPermissionManager::setRemoteDesktopPermission(bool enable)
{
    return setPermission(QString::fromLatin1(PERMISSION_REMOTE_DESKTOP), enable);
}

bool PortalPermissionManager::hasPermission(const QString &permissionId) const
{
    if (!m_permissionStore || !m_permissionStore->isValid()) {
        qWarning() << "PortalPermissionManager: Permission Store not available";
        return false;
    }

    // Call GetPermission(table: s, id: s, app: s) -> permissions: as
    QDBusReply<QStringList> reply = m_permissionStore->call(
        QStringLiteral("GetPermission"),
        QString::fromLatin1(TABLE_NAME),
        permissionId,
        QString::fromLatin1(APP_ID)
    );

    if (!reply.isValid()) {
        // No permission entry means permission not granted
        qDebug() << "PortalPermissionManager: No permission entry for" << permissionId
                 << "- treating as not granted";
        return false;
    }

    QStringList permissions = reply.value();

    // Check if "yes" is in the permissions array
    bool hasPermission = permissions.contains(QStringLiteral("yes"));

    qDebug() << "PortalPermissionManager: Permission" << permissionId
             << "state:" << (hasPermission ? "granted" : "not granted");

    return hasPermission;
}

Result<void> PortalPermissionManager::setPermission(const QString &permissionId, bool enable)
{
    if (!m_permissionStore || !m_permissionStore->isValid()) {
        return Result<void>::error(i18n("Permission Store not available"));
    }

    if (enable) {
        // Grant permission: SetPermission(table: s, create: b, id: s, app: s, permissions: as)
        QStringList permissions;
        permissions << QStringLiteral("yes");

        QDBusReply<void> reply = m_permissionStore->call(
            QStringLiteral("SetPermission"),
            QString::fromLatin1(TABLE_NAME),
            true,  // create if not exists
            permissionId,
            QString::fromLatin1(APP_ID),
            permissions
        );

        if (!reply.isValid()) {
            QString errorMsg = i18n("Failed to grant %1 permission: %2", permissionId, reply.error().message());
            qWarning() << "PortalPermissionManager:" << errorMsg;
            return Result<void>::error(errorMsg);
        }

        qDebug() << "PortalPermissionManager: Granted permission:" << permissionId;
        return Result<void>::success();

    } else {
        // Revoke permission: DeletePermission(table: s, id: s, app: s)
        QDBusReply<void> reply = m_permissionStore->call(
            QStringLiteral("DeletePermission"),
            QString::fromLatin1(TABLE_NAME),
            permissionId,
            QString::fromLatin1(APP_ID)
        );

        if (!reply.isValid()) {
            QString errorMsg = i18n("Failed to revoke %1 permission: %2", permissionId, reply.error().message());
            qWarning() << "PortalPermissionManager:" << errorMsg;
            return Result<void>::error(errorMsg);
        }

        qDebug() << "PortalPermissionManager: Revoked permission:" << permissionId;
        return Result<void>::success();
    }
}

} // namespace YubiKey
} // namespace KRunner
