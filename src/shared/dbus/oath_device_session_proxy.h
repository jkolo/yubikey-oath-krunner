/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OATH_DEVICE_SESSION_PROXY_H
#define OATH_DEVICE_SESSION_PROXY_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include "types/device_state.h"

// Forward declarations
class QDBusInterface;

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Proxy for device session connection state and configuration
 *
 * This class represents a D-Bus object at path:
 * /pl/jkolo/yubikey/oath/devices/<deviceId>
 *
 * Interface: pl.jkolo.yubikey.oath.DeviceSession
 *
 * Single Responsibility: Proxy for device session D-Bus interface
 * - Manages connection lifecycle state (daemon↔device communication)
 * - Handles password validation and storage in KWallet
 * - Tracks device availability (LastSeen timestamp)
 * - Emits signals on session state changes
 *
 * Note: This interface is exposed on the same D-Bus object as OathDeviceProxy
 * but manages orthogonal concerns (session/connection vs device/OATH application).
 *
 * Architecture:
 * ```
 * OathDeviceProxy (pl.jkolo.yubikey.oath.Device) ← device hardware + OATH operations
 *     + ← same D-Bus object path
 * OathDeviceSessionProxy (pl.jkolo.yubikey.oath.DeviceSession) ← connection state
 * ```
 */
class OathDeviceSessionProxy : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs device session proxy from D-Bus object path and properties
     * @param objectPath D-Bus object path (e.g. /pl/jkolo/yubikey/oath/devices/<deviceId>)
     * @param sessionProperties Property map for pl.jkolo.yubikey.oath.DeviceSession interface
     * @param parent Parent object
     *
     * Properties are cached on construction.
     * Creates QDBusInterface for method calls and property monitoring.
     * Connects to D-Bus PropertiesChanged signals.
     */
    explicit OathDeviceSessionProxy(const QString &objectPath,
                                     const QVariantMap &sessionProperties,
                                     QObject *parent = nullptr);

    ~OathDeviceSessionProxy() override;

    // ========== Cached Properties ==========

    [[nodiscard]] QString objectPath() const { return m_objectPath; }

    /**
     * @brief Gets connection lifecycle state
     * @return Current device state (Disconnected, Connecting, Authenticating, FetchingCredentials, Ready, Error)
     */
    [[nodiscard]] DeviceState state() const { return m_state; }

    /**
     * @brief Gets human-readable state message
     * @return State description (e.g. error message when State=Error)
     */
    [[nodiscard]] QString stateMessage() const { return m_stateMessage; }

    /**
     * @brief Checks if daemon has valid password for this session
     * @return true if password stored in KWallet is valid
     */
    [[nodiscard]] bool hasValidPassword() const { return m_hasValidPassword; }

    /**
     * @brief Gets last seen timestamp
     * @return DateTime when device was last detected by daemon
     */
    [[nodiscard]] QDateTime lastSeen() const { return m_lastSeen; }

    /**
     * @brief Helper: checks if device is connected
     * @return true if state != Disconnected
     */
    [[nodiscard]] bool isConnected() const { return m_state != DeviceState::Disconnected; }

    // ========== D-Bus Methods ==========

    /**
     * @brief Saves password for device session
     * @param password Password to test and save to KWallet
     * @return true on success (password valid and saved), false on failure
     *
     * Synchronous D-Bus call to SavePassword().
     * Tests the password by attempting connection to device.
     * Only saves to KWallet if password is valid.
     */
    bool savePassword(const QString &password);

Q_SIGNALS:
    /**
     * @brief Emitted when device state changes
     * @param newState New device state
     */
    void stateChanged(DeviceState newState);

    /**
     * @brief Emitted when device state message changes
     * @param message State error/detail message
     */
    void stateMessageChanged(const QString &message);

    /**
     * @brief Emitted when hasValidPassword property changes
     * @param hasValid New hasValidPassword state
     */
    void hasValidPasswordChanged(bool hasValid);

    /**
     * @brief Emitted when lastSeen timestamp changes
     * @param timestamp New lastSeen timestamp
     */
    void lastSeenChanged(const QDateTime &timestamp);

private Q_SLOTS:
    void onPropertiesChanged(const QString &interfaceName,
                            const QVariantMap &changedProperties,
                            const QStringList &invalidatedProperties);

private:  // NOLINT(readability-redundant-access-specifiers) - Required to close Q_SLOTS section for moc
    void connectToSignals();

    QString m_objectPath;
    QDBusInterface *m_interface;

    // Cached properties
    DeviceState m_state{DeviceState::Disconnected}; // connection lifecycle state
    QString m_stateMessage; // state error/detail message
    bool m_hasValidPassword{false}; // whether daemon has valid password in KWallet
    QDateTime m_lastSeen; // last time device was detected

    static constexpr const char *SERVICE_NAME = "pl.jkolo.yubikey.oath.daemon";
    static constexpr const char *INTERFACE_NAME = "pl.jkolo.yubikey.oath.DeviceSession";
    static constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
};

} // namespace Shared
} // namespace YubiKeyOath

#endif // OATH_DEVICE_SESSION_PROXY_H
