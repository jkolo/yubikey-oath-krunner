/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <memory>
#include "../shared/dbus/yubikey_dbus_types.h"
#include "../shared/types/oath_credential.h"

// Forward declarations
namespace KRunner {
namespace YubiKey {
    class YubiKeyDeviceManager;
    class YubiKeyDatabase;
    class PasswordStorage;
}
}

namespace KRunner {
namespace YubiKey {

/**
 * @brief D-Bus service for YubiKey OATH operations
 *
 * This class implements the D-Bus interface for YubiKey OATH operations.
 * It aggregates YubiKeyDeviceManager, YubiKeyDatabase, and PasswordStorage components
 * and exposes their functionality through D-Bus.
 *
 * Single Responsibility: D-Bus service layer - marshaling between D-Bus and business logic
 */
class YubiKeyDBusService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasma.krunner.yubikey.Device")

public:
    explicit YubiKeyDBusService(QObject *parent = nullptr);
    ~YubiKeyDBusService() override;

public Q_SLOTS:
    /**
     * @brief Lists all known YubiKey devices
     * @return List of device information structures
     *
     * Returns both connected and previously seen devices from database.
     */
    QList<DeviceInfo> ListDevices();

    /**
     * @brief Gets credentials from specific device
     * @param deviceId Device ID (empty string = use first available device)
     * @return List of credential information structures
     */
    QList<CredentialInfo> GetCredentials(const QString &deviceId);

    /**
     * @brief Generates TOTP code for credential
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return Result structure with (code, validUntil timestamp)
     */
    GenerateCodeResult GenerateCode(const QString &deviceId,
                                    const QString &credentialName);

    /**
     * @brief Saves password for device
     * @param deviceId Device ID
     * @param password Password to save
     * @return true if password was saved successfully
     *
     * This method tests the password first, and only saves if valid.
     * Also updates the requires_password flag in database.
     */
    bool SavePassword(const QString &deviceId, const QString &password);

    /**
     * @brief Forgets device - removes from database and deletes password
     * @param deviceId Device ID to forget
     */
    void ForgetDevice(const QString &deviceId);

    /**
     * @brief Sets custom name for device
     * @param deviceId Device ID
     * @param newName New friendly name for device
     * @return true if name was updated successfully
     *
     * Updates device name in database. Name must not be empty after trimming.
     */
    bool SetDeviceName(const QString &deviceId, const QString &newName);

Q_SIGNALS:
    /**
     * @brief Emitted when a YubiKey device is connected
     * @param deviceId Device ID of connected YubiKey
     */
    void DeviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when a YubiKey device is disconnected
     * @param deviceId Device ID of disconnected YubiKey
     */
    void DeviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID with updated credentials
     */
    void CredentialsUpdated(const QString &deviceId);

private Q_SLOTS:
    // Internal signal handlers from YubiKeyDeviceManager
    void onDeviceConnectedInternal(const QString &deviceId);
    void onDeviceDisconnectedInternal(const QString &deviceId);
    void onCredentialCacheFetched(const QString &deviceId,
                                 const QList<OathCredential> &credentials);

private:
    /**
     * @brief Clears device from memory (called directly by ForgetDevice)
     * @param deviceId Device ID to clear
     *
     * This is a private method (not a signal) to avoid D-Bus exposure.
     */
    void clearDeviceFromMemory(const QString &deviceId);
    /**
     * @brief Converts OathCredential to CredentialInfo for D-Bus transfer
     * @param credential OathCredential from YubiKeyDeviceManager
     * @return CredentialInfo for D-Bus
     */
    CredentialInfo convertCredential(const OathCredential &credential) const;

    std::unique_ptr<YubiKeyDeviceManager> m_deviceManager;
    std::unique_ptr<YubiKeyDatabase> m_database;
    std::unique_ptr<PasswordStorage> m_passwordStorage;
};

} // namespace YubiKey
} // namespace KRunner
