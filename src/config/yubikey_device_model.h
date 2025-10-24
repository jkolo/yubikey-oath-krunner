/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "dbus/yubikey_dbus_types.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QList>

namespace KRunner {
namespace YubiKey {

// Forward declarations
class YubiKeyDBusClient;
class PasswordDialog;

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
     * Shows QML PasswordDialog for password entry.
     */
    Q_INVOKABLE void authorizeDevice(const QString &deviceId);

    /**
     * @brief Tests and saves password for device
     * @param deviceId Device ID
     * @param password Password to test
     * @return true if password valid and saved, false otherwise
     *
     * This method is called from PasswordDialog.qml.
     * It tests the password via D-Bus and saves it if valid.
     * Emits passwordTestFailed signal if password invalid.
     */
    Q_INVOKABLE bool testAndSavePassword(const QString &deviceId, const QString &password);

    /**
     * @brief Shows password dialog and handles password entry
     * @param deviceId Device ID
     * @param deviceName Friendly device name
     *
     * Shows password dialog using PasswordDialogHelper. Validates device state first.
     * On success, refreshes device list from daemon. On failure, dialog stays open with error.
     * Called from QML "Authorize" button.
     */
    Q_INVOKABLE void showPasswordDialog(const QString &deviceId,
                                         const QString &deviceName);

    /**
     * @brief Forgets device - removes from daemon database and deletes password
     * @param deviceId Device ID to forget
     *
     * Uses D-Bus ForgetDevice() call to daemon.
     * Called from QML "Forget" button.
     */
    Q_INVOKABLE void forgetDevice(const QString &deviceId);

    /**
     * @brief Sets custom name for device
     * @param deviceId Device ID
     * @param newName New friendly name for device
     * @return true if name was updated successfully
     *
     * Uses D-Bus SetDeviceName() call to daemon.
     * Updates local model and notifies QML on success.
     * Called from QML inline editing.
     */
    Q_INVOKABLE bool setDeviceName(const QString &deviceId, const QString &newName);

Q_SIGNALS:
    /**
     * @brief Emitted when password test failed
     * @param deviceId Device ID
     * @param error Error message
     *
     * Used by PasswordDialog.qml to display error messages.
     */
    void passwordTestFailed(const QString &deviceId, const QString &error);

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
