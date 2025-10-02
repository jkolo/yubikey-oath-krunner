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
