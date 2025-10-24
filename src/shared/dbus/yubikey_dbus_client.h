/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include "yubikey_dbus_types.h"

// Forward declarations
class QDBusInterface;
class QDBusServiceWatcher;

namespace KRunner {
namespace YubiKey {

/**
 * @brief D-Bus client for YubiKey OATH daemon
 *
 * This class provides a client interface to the YubiKey OATH D-Bus service.
 * It handles connection to the daemon, method calls, and signal forwarding.
 *
 * Single Responsibility: D-Bus client layer - marshaling and connection management
 */
class YubiKeyDBusClient : public QObject
{
    Q_OBJECT

public:
    explicit YubiKeyDBusClient(QObject *parent = nullptr);
    ~YubiKeyDBusClient() override;

    /**
     * @brief Lists all known YubiKey devices
     * @return List of device information structures
     *
     * Synchronous D-Bus call. Returns empty list if daemon unavailable.
     */
    QList<DeviceInfo> listDevices();

    /**
     * @brief Gets credentials from specific device
     * @param deviceId Device ID (empty string = use first available device)
     * @return List of credential information structures
     *
     * Synchronous D-Bus call. Returns empty list if daemon unavailable.
     */
    QList<CredentialInfo> getCredentials(const QString &deviceId);

    /**
     * @brief Generates TOTP code for credential
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return Result structure with (code, validUntil timestamp)
     *
     * Synchronous D-Bus call. Returns empty code and 0 if failed.
     */
    GenerateCodeResult generateCode(const QString &deviceId,
                                    const QString &credentialName);

    /**
     * @brief Saves password for device
     * @param deviceId Device ID
     * @param password Password to save
     * @return true if password was saved successfully
     *
     * Synchronous D-Bus call. Returns false if daemon unavailable.
     */
    bool savePassword(const QString &deviceId, const QString &password);

    /**
     * @brief Forgets device - removes from database and deletes password
     * @param deviceId Device ID to forget
     *
     * Synchronous D-Bus call. No return value.
     */
    void forgetDevice(const QString &deviceId);

    /**
     * @brief Sets custom name for device
     * @param deviceId Device ID
     * @param newName New friendly name for device
     * @return true if name was updated successfully
     *
     * Synchronous D-Bus call. Returns false if daemon unavailable or update failed.
     */
    bool setDeviceName(const QString &deviceId, const QString &newName);

    /**
     * @brief Gets device name from daemon
     * @param deviceId Device ID
     * @return Device name if found, or deviceId as fallback
     *
     * Synchronous D-Bus call. Queries listDevices() and searches for matching device.
     * Returns deviceId as fallback if daemon unavailable or device not found.
     */
    QString getDeviceName(const QString &deviceId);

    /**
     * @brief Adds or updates OATH credential on YubiKey
     * @param deviceId Device ID (empty string = use first available device)
     * @param name Full credential name (issuer:account)
     * @param secret Base32-encoded secret key
     * @param type Credential type ("TOTP" or "HOTP")
     * @param algorithm Hash algorithm ("SHA1", "SHA256", or "SHA512")
     * @param digits Number of digits (6-8)
     * @param period TOTP period in seconds (default 30, ignored for HOTP)
     * @param counter Initial HOTP counter value (ignored for TOTP)
     * @param requireTouch Whether to require physical touch
     * @return Empty string on success, error message on failure
     *
     * Synchronous D-Bus call. Returns error message if daemon unavailable or operation failed.
     */
    QString addCredential(const QString &deviceId,
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
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Synchronous D-Bus call. Delegates to daemon's CopyCodeToClipboard method.
     * Generates code and copies to clipboard with auto-clear support.
     * Shows notification if enabled in configuration.
     */
    bool copyCodeToClipboard(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Types TOTP code via keyboard emulation
     * @param deviceId Device ID (empty string = use first available device)
     * @param credentialName Full credential name (issuer:username)
     * @return true on success, false on failure
     *
     * Synchronous D-Bus call. Delegates to daemon's TypeCode method.
     * Generates code and types it using appropriate input method (Portal/Wayland/X11).
     * Handles touch requirements with user notifications.
     */
    bool typeCode(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Starts workflow to add credential from screenshot QR code
     * @return QVariantMap with keys: "success" (bool), "error" (string, optional)
     *
     * Synchronous D-Bus call. Delegates to daemon's AddCredentialFromScreen method.
     * Captures screenshot, parses QR code, shows dialog, and saves to YubiKey.
     */
    QVariantMap addCredentialFromScreen();

    /**
     * @brief Checks if daemon is currently available
     * @return true if daemon is registered on D-Bus
     */
    bool isDaemonAvailable() const;

Q_SIGNALS:
    /**
     * @brief Emitted when a YubiKey device is connected
     * @param deviceId Device ID of connected YubiKey
     *
     * Forwarded from daemon D-Bus signal.
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when a YubiKey device is disconnected
     * @param deviceId Device ID of disconnected YubiKey
     *
     * Forwarded from daemon D-Bus signal.
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID with updated credentials
     *
     * Forwarded from daemon D-Bus signal.
     */
    void credentialsUpdated(const QString &deviceId);

    /**
     * @brief Emitted when device forget is requested
     * @param deviceId Device ID to forget
     *
     * Forwarded from daemon D-Bus signal.
     * This signal is emitted BEFORE the device is actually removed.
     */
    void deviceForgetRequested(const QString &deviceId);

    /**
     * @brief Emitted when daemon becomes unavailable
     *
     * This signal is emitted when the daemon exits or becomes unregistered.
     */
    void daemonUnavailable();

private Q_SLOTS:
    void onDBusServiceRegistered(const QString &serviceName);
    void onDBusServiceUnregistered(const QString &serviceName);

private:
    void setupSignalConnections();
    void checkDaemonAvailability();

    QDBusInterface *m_interface;
    QDBusServiceWatcher *m_serviceWatcher;
    bool m_daemonAvailable;

    static constexpr const char *SERVICE_NAME = "org.kde.plasma.krunner.yubikey";
    static constexpr const char *OBJECT_PATH = "/Device";
    static constexpr const char *INTERFACE_NAME = "org.kde.plasma.krunner.yubikey.Device";
};

} // namespace YubiKey
} // namespace KRunner
