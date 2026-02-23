/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <memory>
#include "types/yubikey_value_types.h"
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"

// Forward declarations
namespace YubiKeyOath {
namespace Daemon {
    class OathDeviceManager;
    class OathDatabase;
    class SecretStorage;
    class DaemonConfiguration;
    class OathActionCoordinator;
    class OathDevice;
    class PasswordService;
    class DeviceLifecycleService;
    class CredentialService;
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
 * OathDBusService delegates to this class for all actual operations.
 *
 * @par Architecture
 * ```
 * OathDBusService (D-Bus layer)
 *     ↓ delegates
 * OathService (business logic) ← YOU ARE HERE
 *     ↓ uses
 * Components (DeviceManager, Database, SecretStorage...)
 * ```
 */
class OathService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKey service with all components
     * @param parent Parent QObject
     */
    explicit OathService(QObject *parent = nullptr);
    ~OathService() override;

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
     * @brief Gets all credentials from all connected devices
     * @return List of credentials from all devices
     */
    QList<OathCredential> getCredentials();

    /**
     * @brief Gets device instance by ID
     * @param deviceId Device ID to retrieve
     * @return Pointer to device or nullptr if not found
     */
    OathDevice* getDevice(const QString &deviceId);

    /**
     * @brief Gets device manager instance
     * @return Pointer to OathDeviceManager (not owned)
     *
     * Used to access device manager for starting monitoring after D-Bus initialization.
     */
    OathDeviceManager* getDeviceManager() const { return m_deviceManager.get(); }

    /**
     * @brief Gets credential service for async operations
     * @return Pointer to CredentialService (not owned)
     */
    CredentialService* getCredentialService() const;

    /**
     * @brief Gets action coordinator for direct action execution
     * @return Pointer to OathActionCoordinator (not owned)
     *
     * Use this for async workflows where you need to execute copy/type
     * operations after code generation is complete (e.g., from D-Bus objects).
     */
    OathActionCoordinator* getActionCoordinator() const { return m_actionCoordinator.get(); }

    /**
     * @brief Gets IDs of all currently connected devices
     * @return List of connected device IDs
     */
    QList<QString> getConnectedDeviceIds() const;

    /**
     * @brief Gets last seen timestamp for device
     * @param deviceId Device ID
     * @return QDateTime timestamp (unix epoch in ms) or invalid QDateTime if device not in database
     */
    QDateTime getDeviceLastSeen(const QString &deviceId) const;

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
     * @brief Changes password on YubiKey
     * @param deviceId Device ID
     * @param oldPassword Current password
     * @param newPassword New password (empty string removes password)
     * @return true on success, false on failure
     *
     * Flow:
     * 1. Authenticates with old password
     * 2. Sets new password via SET_CODE (or removes if newPassword empty)
     * 3. Updates KWallet with new password (or removes entry)
     * 4. Updates database and emits signals
     *
     * If newPassword is empty, password protection is removed from YubiKey.
     */
    bool changePassword(const QString &deviceId,
                       const QString &oldPassword,
                       const QString &newPassword);

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
     * @brief Deletes credential from YubiKey
     * @param deviceId Device ID (empty = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Removes credential from device.
     * Requires authentication if YubiKey is password protected.
     * Emits credentialsUpdated signal on success.
     */
    bool deleteCredential(const QString &deviceId, const QString &credentialName);

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
    // ========================================================================
    // INTERFACE SIGNALS (must be in same order as ICredentialUpdateNotifier)
    // ========================================================================

    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID
     */
    void credentialsUpdated(const QString &deviceId);

    /**
     * @brief Emitted when a device is connected
     * @param deviceId Device ID
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when device connected and successfully authenticated
     * @param deviceId Device ID that was authenticated
     *
     * This signal is emitted AFTER successful authentication AND credential fetch.
     * Guarantees device is ready with valid credentials.
     */
    void deviceConnectedAndAuthenticated(const QString &deviceId);

    /**
     * @brief Emitted when device connected but authentication failed
     * @param deviceId Device ID that failed authentication
     * @param error Error message describing authentication failure
     *
     * This signal is emitted when authentication fails (wrong password or no password available).
     */
    void deviceConnectedAuthenticationFailed(const QString &deviceId, const QString &error);

    // ========================================================================
    // IMPLEMENTATION-SPECIFIC SIGNALS (not in interface)
    // ========================================================================

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

private Q_SLOTS:
    void onCredentialCacheFetched(const QString &deviceId,
                                 const QList<OathCredential> &credentials);
    void onReconnectStarted(const QString &deviceId);
    void onReconnectCompleted(const QString &deviceId, bool success);
    void onConfigurationChanged();

private:

    /**
     * @brief Checks authentication state based on credentials and password
     * @param deviceId Device ID
     * @param device Device instance
     * @param credentials Fetched credentials list
     * @param authenticationFailed Output parameter set to true if auth failed
     * @param authError Output parameter set to error message if auth failed
     */
    void checkAuthenticationState(const QString &deviceId,
                                  OathDevice *device,
                                  const QList<OathCredential> &credentials,
                                  bool &authenticationFailed,
                                  QString &authError);

    /**
     * @brief Handles authentication failure - emits signals
     * @param deviceId Device ID
     * @param authError Error message
     */
    void handleAuthenticationFailure(const QString &deviceId,
                                     const QString &authError);

    /**
     * @brief Handles authentication success - saves credentials and emits signals
     * @param deviceId Device ID
     * @param device Device instance
     * @param credentials Fetched credentials list
     */
    void handleAuthenticationSuccess(const QString &deviceId,
                                     OathDevice *device,
                                     const QList<OathCredential> &credentials);

    /**
     * @brief Determines if credentials should be saved to cache with rate limiting
     * @param deviceId Device ID
     * @return true if credentials should be saved
     */
    bool shouldSaveCredentialsToCache(const QString &deviceId);

    /**
     * @brief Gets list of all known devices (connected and disconnected)
     * @return List of device info with isConnected flag
     */
    QList<DeviceInfo> getAvailableDevices();

    /**
     * @brief Validates credential data before saving to device
     * @param data Credential data to validate
     * @param selectedDeviceId Device ID
     * @param errorMessage Output parameter for error message
     * @return OathDevice* if validation passed, nullptr otherwise
     */
    OathDevice* validateCredentialBeforeSave(const OathCredentialData &data,
                                                     const QString &selectedDeviceId,
                                                     QString &errorMessage);

    // Reconnect notification state
    uint m_reconnectNotificationId = 0;

    // Rate limiting for credential saves (per device)
    QHash<QString, qint64> m_lastCredentialSave;
    mutable QMutex m_lastCredentialSaveMutex;

    std::unique_ptr<OathDeviceManager> m_deviceManager;
    std::unique_ptr<OathDatabase> m_database;
    std::unique_ptr<SecretStorage> m_secretStorage;
    std::unique_ptr<DaemonConfiguration> m_config;
    std::unique_ptr<OathActionCoordinator> m_actionCoordinator;
    std::unique_ptr<PasswordService> m_passwordService;
    std::unique_ptr<DeviceLifecycleService> m_deviceLifecycleService;
    std::unique_ptr<CredentialService> m_credentialService;
};

} // namespace Daemon
} // namespace YubiKeyOath
