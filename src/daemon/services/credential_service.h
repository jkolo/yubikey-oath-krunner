/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"
#include "types/yubikey_value_types.h"
#include "../../shared/config/configuration_provider.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathDeviceManager;
class OathDatabase;
class OathDevice;
class DBusNotificationManager;

/**
 * @brief Service responsible for credential operations on YubiKey devices
 *
 * Handles CRUD operations for OATH credentials:
 * - Retrieving credentials from connected/offline devices
 * - Generating TOTP/HOTP codes
 * - Adding new credentials (interactive dialog and automatic modes)
 * - Deleting credentials from devices
 *
 * Extracted from OathService to follow Single Responsibility Principle.
 */
class CredentialService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct CredentialService
     * @param deviceManager Device manager for accessing YubiKey hardware
     * @param database Database for credential caching
     * @param config Configuration for cache and notification settings
     * @param parent Parent QObject
     */
    explicit CredentialService(OathDeviceManager *deviceManager,
                              OathDatabase *database,
                              Shared::ConfigurationProvider *config,
                              QObject *parent = nullptr);

    ~CredentialService() override;

    /**
     * @brief Gets credentials from specific device or all devices
     * @param deviceId Device ID (empty = all devices)
     * @return List of credentials (includes cached for offline devices)
     */
    QList<Shared::OathCredential> getCredentials(const QString &deviceId);

    /**
     * @brief Gets all credentials from all connected devices
     * @return List of credentials from all devices
     */
    QList<Shared::OathCredential> getCredentials();

    /**
     * @brief Generates TOTP/HOTP code for credential
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @return Result with code and validUntil timestamp
     */
    Shared::GenerateCodeResult generateCode(const QString &deviceId,
                                            const QString &credentialName);

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
    Shared::AddCredentialResult addCredential(const QString &deviceId,
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
     * @param deviceId Device ID
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Removes credential from device.
     * Requires authentication if YubiKey is password protected.
     */
    bool deleteCredential(const QString &deviceId, const QString &credentialName);

    // === ASYNC API (for new D-Bus interface in Phase 4) ===

    /**
     * @brief Generates TOTP/HOTP code asynchronously
     * @param deviceId Device ID
     * @param credentialName Full credential name
     *
     * Returns immediately. Result emitted via codeGenerated signal.
     * Non-blocking - uses worker pool for PC/SC operations.
     */
    void generateCodeAsync(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Deletes credential asynchronously
     * @param deviceId Device ID
     * @param credentialName Full credential name
     *
     * Returns immediately. Result emitted via credentialDeleted signal.
     * Non-blocking - uses worker pool for PC/SC operations.
     */
    void deleteCredentialAsync(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Copies code to clipboard asynchronously
     * @param deviceId Device ID
     * @param credentialName Full credential name
     *
     * Returns immediately. Result emitted via clipboardCopied signal.
     * Generates code, then copies to clipboard.
     */
    void copyCodeToClipboardAsync(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Types code via input system asynchronously
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @param fallbackToCopy If true, copies to clipboard if typing fails
     *
     * Returns immediately. Result emitted via codeTyped signal.
     * Generates code, then types it.
     */
    void typeCodeAsync(const QString &deviceId, const QString &credentialName, bool fallbackToCopy);

Q_SIGNALS:
    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID
     */
    void credentialsUpdated(const QString &deviceId);

    /**
     * @brief Emitted when async code generation completes
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param code Generated code (empty if error)
     * @param validUntil Unix timestamp when code expires (0 if error)
     * @param error Error message (empty if success)
     */
    void codeGenerated(const QString &deviceId,
                      const QString &credentialName,
                      const QString &code,
                      qint64 validUntil,
                      const QString &error);

    /**
     * @brief Emitted when async credential deletion completes
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param success true if deleted successfully
     * @param error Error message (empty if success)
     */
    void credentialDeleted(const QString &deviceId,
                          const QString &credentialName,
                          bool success,
                          const QString &error);

    /**
     * @brief Emitted when async clipboard copy completes
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param success true if copied successfully
     * @param error Error message (empty if success)
     */
    void clipboardCopied(const QString &deviceId,
                        const QString &credentialName,
                        bool success,
                        const QString &error);

    /**
     * @brief Emitted when async code typing completes
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param success true if typed successfully
     * @param error Error message (empty if success)
     */
    void codeTyped(const QString &deviceId,
                  const QString &credentialName,
                  bool success,
                  const QString &error);

private:
    /**
     * @brief Appends cached credentials for offline device to list
     * @param deviceId Device ID to check
     * @param credentialsList List to append to (modified in-place)
     *
     * If cache is enabled and device is offline, appends cached credentials.
     * Skips if device is currently connected (already in list).
     */
    void appendCachedCredentialsForOfflineDevice(const QString &deviceId,
                                                  QList<Shared::OathCredential> &credentialsList);

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
                                      const Shared::OathCredentialData &initialData);

    /**
     * @brief Gets list of all known devices (connected and disconnected)
     * @return List of device info with isConnected flag
     */
    QList<Shared::DeviceInfo> getAvailableDevices();

    /**
     * @brief Validates credential data before saving to device
     * @param data Credential data to validate
     * @param selectedDeviceId Device ID
     * @param errorMessage Output parameter for error message
     * @return OathDevice* if validation passed, nullptr otherwise
     */
    OathDevice* validateCredentialBeforeSave(const Shared::OathCredentialData &data,
                                                     const QString &selectedDeviceId,
                                                     QString &errorMessage);

    OathDeviceManager *m_deviceManager;  // Not owned
    OathDatabase *m_database;            // Not owned
    Shared::ConfigurationProvider *m_config;          // Not owned
    std::unique_ptr<DBusNotificationManager> m_notificationManager;  // Owned

    // Active add credential dialogs (kept alive until user closes or save completes)
    QList<class AddCredentialDialog*> m_activeDialogs;
};

} // namespace Daemon
} // namespace YubiKeyOath
