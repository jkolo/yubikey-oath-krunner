/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../shared/dbus/yubikey_dbus_types.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QList>

namespace KRunner {
namespace YubiKey {

// Forward declarations
class YubiKeyDBusClient;

/**
 * @brief Model for displaying YubiKey devices in configuration UI
 *
 * This model manages the list of known YubiKey devices, combining:
 * - Currently connected devices (from YubiKeyDeviceManager)
 * - Previously seen devices (from PasswordStorage/KWallet)
 *
 * Provides real-time updates when devices are connected/disconnected.
 */
class YubiKeyDeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /**
     * @brief QML role names for device properties
     */
    enum DeviceRoles {
        DeviceIdRole = Qt::UserRole + 1,
        DeviceNameRole,
        IsConnectedRole,
        RequiresPasswordRole,
        HasValidPasswordRole,
        ShowAuthorizeButtonRole
    };

    /**
     * @brief Constructs device model
     * @param dbusClient YubiKeyDBusClient instance for D-Bus communication with daemon
     * @param parent Parent QObject
     */
    explicit YubiKeyDeviceModel(YubiKeyDBusClient *dbusClient, QObject *parent = nullptr);

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief Refreshes device list from daemon
     *
     * Fetches all known devices from daemon via D-Bus ListDevices() call.
     * Daemon merges connected and previously seen devices.
     */
    Q_INVOKABLE void refreshDevices();

    /**
     * @brief Opens password dialog and authorizes device
     * @param deviceId Device ID to authorize
     *
     * This method is called from QML when user clicks "Authorize" button.
     * Shows native kdialog for password entry (same as KRunner plugin).
     * Recursively re-prompts on invalid password.
     */
    Q_INVOKABLE void authorizeDevice(const QString &deviceId);

    /**
     * @brief Forgets device - removes from daemon database and deletes password
     * @param deviceId Device ID to forget
     *
     * Uses D-Bus ForgetDevice() call to daemon.
     * Called from QML "Forget" button.
     */
    Q_INVOKABLE void forgetDevice(const QString &deviceId);

Q_SIGNALS:

private Q_SLOTS:
    /**
     * @brief Handles device connection event from daemon
     * @param deviceId Device ID that was connected
     */
    void onDeviceConnected(const QString &deviceId);

    /**
     * @brief Handles device disconnection event from daemon
     * @param deviceId Device ID that was disconnected
     */
    void onDeviceDisconnected(const QString &deviceId);

    /**
     * @brief Handles credentials update from daemon
     * @param deviceId Device ID with updated credentials
     */
    void onCredentialsUpdated(const QString &deviceId);

private:
    YubiKeyDBusClient *m_dbusClient;
    QList<DeviceInfo> m_devices;

    /**
     * @brief Shows password dialog using kdialog (same as KRunner)
     * @param deviceId Device ID requiring password
     * @param errorMessage Optional error message to display (e.g., "Invalid password")
     *
     * Uses QProcess to launch kdialog --password.
     * Recursively calls itself on invalid password until success or cancel.
     */
    void showPasswordDialogForDevice(const QString &deviceId, const QString &errorMessage);

    /**
     * @brief Finds device info by ID
     * @param deviceId Device ID to find
     * @return Pointer to DeviceInfo or nullptr if not found
     */
    DeviceInfo* findDevice(const QString &deviceId);

    /**
     * @brief Finds device index by ID
     * @param deviceId Device ID to find
     * @return Index in m_devices or -1 if not found
     */
    int findDeviceIndex(const QString &deviceId) const;
};

} // namespace YubiKey
} // namespace KRunner
