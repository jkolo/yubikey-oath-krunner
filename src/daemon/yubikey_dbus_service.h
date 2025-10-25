/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <memory>
#include "dbus/yubikey_dbus_types.h"
#include "types/oath_credential.h"

// Forward declarations
namespace KRunner {
namespace YubiKey {
    class YubiKeyService;
}
}

namespace KRunner {
namespace YubiKey {

/**
 * @brief D-Bus service for YubiKey OATH operations (thin marshaling layer)
 *
 * Single Responsibility: D-Bus marshaling - convert between D-Bus types and business logic
 *
 * This is a THIN layer that:
 * 1. Receives D-Bus method calls
 * 2. Converts D-Bus types to internal types (using TypeConversions)
 * 3. Delegates to YubiKeyService (business logic layer)
 * 4. Converts results back to D-Bus types
 * 5. Forwards signals from YubiKeyService
 *
 * @par Architecture
 * ```
 * D-Bus Client
 *     ↓ calls
 * YubiKeyDBusService (marshaling) ← YOU ARE HERE
 *     ↓ delegates
 * YubiKeyService (business logic)
 * ```
 *
 * NO business logic should be in this class!
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

    /**
     * @brief Copies TOTP code to clipboard
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Generates code and copies to clipboard with auto-clear support.
     * Shows notification if enabled in configuration.
     */
    bool CopyCodeToClipboard(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Types TOTP code via keyboard emulation
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Generates code and types it using appropriate input method (Portal/Wayland/X11).
     * Handles touch requirements with user notifications.
     */
    bool TypeCode(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Adds or updates OATH credential on YubiKey
     * @param deviceId Device ID (empty = show dialog to select)
     * @param name Full credential name (issuer:account, empty = show dialog)
     * @param secret Base32-encoded secret key (empty = show dialog)
     * @param type Credential type ("TOTP" or "HOTP", empty = default "TOTP")
     * @param algorithm Hash algorithm ("SHA1", "SHA256", "SHA512", empty = default "SHA1")
     * @param digits Number of digits (6-8, 0 = default 6)
     * @param period TOTP period in seconds (0 = default 30, ignored for HOTP)
     * @param counter Initial HOTP counter value (ignored for TOTP)
     * @param requireTouch Whether to require physical touch (default false)
     * @return AddCredentialResult with status and message
     *
     * Supports two modes:
     * - **Interactive mode**: If deviceId, name, or secret are empty → shows dialog asynchronously
     *   Returns status="Interactive" immediately, dialog shown in background
     *   Dialog allows manual entry or QR code scanning via "Scan QR" button
     * - **Automatic mode**: If all required fields provided → adds directly without dialog
     *   Returns status="Success" or status="Error" with message
     *
     * Requires authentication if YubiKey is password protected.
     */
    AddCredentialResult AddCredential(const QString &deviceId,
                                     const QString &name,
                                     const QString &secret,
                                     const QString &type,
                                     const QString &algorithm,
                                     int digits,
                                     int period,
                                     int counter,
                                     bool requireTouch);

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

private:
    std::unique_ptr<YubiKeyService> m_service;
};

} // namespace YubiKey
} // namespace KRunner
