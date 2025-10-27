/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <memory>
#include "types/yubikey_value_types.h"
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"

// Forward declarations
namespace YubiKeyOath {
namespace Daemon {
    class YubiKeyDeviceManager;
    class YubiKeyDatabase;
    class PasswordStorage;
    class DaemonConfiguration;
    class YubiKeyActionCoordinator;
}
}

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Business logic service for YubiKey operations
 *
 * Single Responsibility: Aggregate and coordinate YubiKey business logic
 * - Device management (adding, removing, naming)
 * - Credential operations (listing, generating codes)
 * - Password management (saving, loading, validation)
 * - Component lifecycle management
 *
 * This is the business logic layer, separate from D-Bus marshaling.
 * YubiKeyDBusService delegates to this class for all actual operations.
 *
 * @par Architecture
 * ```
 * YubiKeyDBusService (D-Bus layer)
 *     ↓ delegates
 * YubiKeyService (business logic) ← YOU ARE HERE
 *     ↓ uses
 * Components (DeviceManager, Database, PasswordStorage...)
 * ```
 */
class YubiKeyService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKey service with all components
     * @param parent Parent QObject
     */
    explicit YubiKeyService(QObject *parent = nullptr);
    ~YubiKeyService() override;

    /**
     * @brief Lists all known YubiKey devices (connected + database)
     * @return List of device information
     *
     * Merges connected devices with database records, generating
     * default names for new devices.
     */
    QList<DeviceInfo> listDevices();

    /**
     * @brief Gets credentials from specific device or all devices
     * @param deviceId Device ID (empty = all devices)
     * @return List of credentials
     */
    QList<OathCredential> getCredentials(const QString &deviceId);

    /**
     * @brief Generates TOTP/HOTP code for credential
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @return Result with code and validUntil timestamp
     */
    GenerateCodeResult generateCode(const QString &deviceId,
                                    const QString &credentialName);

    /**
     * @brief Saves and validates password for device
     * @param deviceId Device ID
     * @param password Password to save
     * @return true if password valid and saved
     */
    bool savePassword(const QString &deviceId, const QString &password);

    /**
     * @brief Forgets device - removes from database and memory
     * @param deviceId Device ID
     */
    void forgetDevice(const QString &deviceId);

    /**
     * @brief Sets custom name for device
     * @param deviceId Device ID
     * @param newName New name (must not be empty after trim)
     * @return true on success
     */
    bool setDeviceName(const QString &deviceId, const QString &newName);

    /**
     * @brief Adds OATH credential to device
     * @param deviceId Device ID
     * @param name Credential name
     * @param secret Base32-encoded secret
     * @param type "TOTP" or "HOTP"
     * @param algorithm "SHA1", "SHA256", or "SHA512"
     * @param digits 6-8
     * @param period TOTP period (seconds)
     * @param counter HOTP counter
     * @param requireTouch Require physical touch
     * @return AddCredentialResult with status and message
     *
     * Returns immediately:
     * - status="Success": Credential added successfully
     * - status="Interactive": Dialog shown asynchronously (non-blocking)
     * - status="Error": Error occurred with message
     */
    AddCredentialResult addCredential(const QString &deviceId,
                                     const QString &name,
                                     const QString &secret,
                                     const QString &type,
                                     const QString &algorithm,
                                     int digits,
                                     int period,
                                     int counter,
                                     bool requireTouch);

    /**
     * @brief Copies TOTP code to clipboard
     * @param deviceId Device ID (empty = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Generates code and copies to clipboard with auto-clear support.
     * Shows notification if enabled in configuration.
     */
    bool copyCodeToClipboard(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Types TOTP code via keyboard emulation
     * @param deviceId Device ID (empty = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Generates code and types it using appropriate input method (Portal/Wayland/X11).
     * Handles touch requirements with user notifications.
     */
    bool typeCode(const QString &deviceId, const QString &credentialName);

Q_SIGNALS:
    /**
     * @brief Emitted when a device is connected
     * @param deviceId Device ID
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when a device is disconnected (physically removed)
     * @param deviceId Device ID
     *
     * Device object should remain on D-Bus with IsConnected=false
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when a device is forgotten (removed from config)
     * @param deviceId Device ID
     *
     * Device object should be completely removed from D-Bus
     */
    void deviceForgotten(const QString &deviceId);

    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID
     */
    void credentialsUpdated(const QString &deviceId);

private Q_SLOTS:
    void onDeviceConnectedInternal(const QString &deviceId);
    void onDeviceDisconnectedInternal(const QString &deviceId);
    void onCredentialCacheFetched(const QString &deviceId,
                                 const QList<OathCredential> &credentials);

private:
    /**
     * @brief Generates default device name
     * @param deviceId Device ID
     * @return "YubiKey <deviceId>"
     */
    QString generateDefaultDeviceName(const QString &deviceId) const;

    /**
     * @brief Clears device from memory
     * @param deviceId Device ID
     */
    void clearDeviceFromMemory(const QString &deviceId);

    /**
     * @brief Shows add credential dialog asynchronously (non-blocking)
     * @param deviceId Preselected device ID
     * @param initialData Initial credential data
     *
     * Dialog is shown in background, doesn't block D-Bus call.
     * On accept: adds credential and emits credentialsUpdated signal.
     * On reject: dialog is closed and deleted.
     */
    void showAddCredentialDialogAsync(const QString &deviceId,
                                      const OathCredentialData &initialData);

    std::unique_ptr<YubiKeyDeviceManager> m_deviceManager;
    std::unique_ptr<YubiKeyDatabase> m_database;
    std::unique_ptr<PasswordStorage> m_passwordStorage;
    std::unique_ptr<DaemonConfiguration> m_config;
    std::unique_ptr<YubiKeyActionCoordinator> m_actionCoordinator;
};

} // namespace Daemon
} // namespace YubiKeyOath
