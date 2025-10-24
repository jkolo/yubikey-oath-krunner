/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PORTAL_PERMISSION_MANAGER_H
#define PORTAL_PERMISSION_MANAGER_H

#include <QObject>
#include <QString>
#include <QDBusInterface>
#include <memory>
#include "../common/result.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Manages XDG Portal permissions via the Permission Store D-Bus interface
 *
 * This class provides methods to query and modify portal permissions stored in the
 * kde-authorized table. It's used to grant/revoke permanent access to screenshot
 * and remote desktop (text input) portals, eliminating interactive permission dialogs.
 *
 * Permissions are stored in the XDG Desktop Portal Permission Store:
 * - Service: org.freedesktop.impl.portal.PermissionStore
 * - Table: "kde-authorized"
 * - App ID: "" (empty for host applications)
 * - Permission IDs: "screenshot", "remote-desktop"
 *
 * @see https://develop.kde.org/docs/administration/portal-permissions/
 */
class PortalPermissionManager : public QObject
{
    Q_OBJECT

public:
    explicit PortalPermissionManager(QObject *parent = nullptr);
    ~PortalPermissionManager() override;

    /**
     * @brief Checks if screenshot permission is granted
     * @return true if permission is granted, false otherwise
     */
    bool hasScreenshotPermission() const;

    /**
     * @brief Checks if remote desktop permission is granted
     * @return true if permission is granted, false otherwise
     */
    bool hasRemoteDesktopPermission() const;

    /**
     * @brief Sets screenshot permission
     * @param enable true to grant permission, false to revoke
     * @return Result with success or error message
     */
    Result<void> setScreenshotPermission(bool enable);

    /**
     * @brief Sets remote desktop permission
     * @param enable true to grant permission, false to revoke
     * @return Result with success or error message
     */
    Result<void> setRemoteDesktopPermission(bool enable);

private:
    /**
     * @brief Gets permission state for a specific portal
     * @param permissionId "screenshot" or "remote-desktop"
     * @return true if permission is granted, false otherwise
     */
    bool hasPermission(const QString &permissionId) const;

    /**
     * @brief Sets permission state for a specific portal
     * @param permissionId "screenshot" or "remote-desktop"
     * @param enable true to grant permission, false to revoke
     * @return Result with success or error message
     */
    Result<void> setPermission(const QString &permissionId, bool enable);

    std::unique_ptr<QDBusInterface> m_permissionStore;

    // Constants
    static constexpr const char* PERMISSION_STORE_SERVICE = "org.freedesktop.impl.portal.PermissionStore";
    static constexpr const char* PERMISSION_STORE_PATH = "/org/freedesktop/impl/portal/PermissionStore";
    static constexpr const char* PERMISSION_STORE_INTERFACE = "org.freedesktop.impl.portal.PermissionStore";
    static constexpr const char* TABLE_NAME = "kde-authorized";
    static constexpr const char* APP_ID = "yubikey-oath-daemon"; // Daemon-specific app ID for portal permissions
    static constexpr const char* PERMISSION_SCREENSHOT = "screenshot";
    static constexpr const char* PERMISSION_REMOTE_DESKTOP = "remote-desktop";
};

} // namespace YubiKey
} // namespace KRunner

#endif // PORTAL_PERMISSION_MANAGER_H
