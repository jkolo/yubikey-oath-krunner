/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OATH_DEVICE_PROXY_H
#define OATH_DEVICE_PROXY_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QDateTime>
#include "types/yubikey_value_types.h"
#include "types/device_state.h"
#include "oath_credential_proxy.h"

// Forward declarations
class QDBusInterface;
class QDBusObjectPath;

namespace YubiKeyOath {
namespace Shared {

class OathDeviceSessionProxy; // Forward declaration

/**
 * @brief Proxy for a single YubiKey device with its credentials
 *
 * This class represents a D-Bus object at path:
 * /pl/jkolo/yubikey/oath/devices/<deviceId>
 *
 * Interface: pl.jkolo.yubikey.oath.Device
 *
 * Single Responsibility: Proxy for device D-Bus object
 * - Caches device properties (some mutable: Name, IsConnected, etc.)
 * - Owns and manages credential proxy objects (children)
 * - Provides methods: SavePassword, ChangePassword, Forget, AddCredential
 * - Converts to DeviceInfo value type
 * - Emits signals on property changes
 *
 * Architecture:
 * ```
 * OathManagerProxy (singleton)
 *     ↓ owns
 * OathDeviceProxy (per device) ← YOU ARE HERE
 *     ↓ owns
 * OathCredentialProxy (per credential)
 * ```
 */
class OathDeviceProxy : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs device proxy from D-Bus object path and properties
     * @param objectPath D-Bus object path (e.g. /pl/jkolo/yubikey/oath/devices/<deviceId>)
     * @param deviceProperties Property map from GetManagedObjects() for pl.jkolo.yubikey.oath.Device interface
     * @param credentialObjects Map of credential object paths → properties from GetManagedObjects()
     * @param parent Parent object (typically OathManagerProxy)
     *
     * Properties are cached on construction.
     * Creates QDBusInterface for method calls and property monitoring.
     * Creates credential proxy objects for all initial credentials.
     * Connects to D-Bus signals: CredentialAdded, CredentialRemoved, PropertiesChanged.
     */
    explicit OathDeviceProxy(const QString &objectPath,
                              const QVariantMap &deviceProperties,
                              const QHash<QString, QVariantMap> &credentialObjects,
                              QObject *parent = nullptr);

    ~OathDeviceProxy() override;

    // ========== Cached Properties ==========

    [[nodiscard]] QString objectPath() const { return m_objectPath; }
    [[nodiscard]] QString deviceId() const { return m_deviceId; } // const property - hex device ID
    [[nodiscard]] QString name() const { return m_name; } // writable property
    [[nodiscard]] quint32 serialNumber() const { return m_serialNumber; } // const property
    [[nodiscard]] QString deviceModel() const { return m_deviceModel; } // const property - human-readable
    [[nodiscard]] YubiKeyModel deviceModelCode() const { return m_deviceModelCode; } // const property - numeric code
    [[nodiscard]] QString formFactor() const { return m_formFactor; } // const property - human-readable
    [[nodiscard]] QStringList capabilities() const { return m_capabilities; } // const property
    [[nodiscard]] bool requiresPassword() const { return m_requiresPassword; }

    // Note: credentialCount() removed - use credentials().size() instead
    // Note: isConnected(), hasValidPassword(), lastSeen(), state(), stateMessage() moved to OathDeviceSessionProxy

    // ========== Credential Management ==========

    /**
     * @brief Gets all credential proxies
     * @return List of credential proxy pointers (owned by this object)
     */
    QList<OathCredentialProxy*> credentials() const;

    /**
     * @brief Gets specific credential by name
     * @param credentialName Full credential name (issuer:username)
     * @return Credential proxy pointer or nullptr if not found
     */
    OathCredentialProxy* getCredential(const QString &credentialName) const;

    // ========== D-Bus Methods ==========
    // Note: SavePassword() moved to OathDeviceSessionProxy

    /**
     * @brief Changes device password
     * @param oldPassword Current password
     * @param newPassword New password
     * @return true on success, false on failure
     *
     * Synchronous D-Bus call to ChangePassword().
     * Updates YubiKey password and KWallet entry.
     */
    bool changePassword(const QString &oldPassword, const QString &newPassword);
    bool changePassword(const QString &oldPassword, const QString &newPassword, QString &errorMessage);

    /**
     * @brief Forgets device - removes from database and KWallet
     *
     * Synchronous D-Bus call to Forget().
     * After successful forget, this proxy becomes invalid.
     * Parent ManagerProxy will emit deviceDisconnected signal.
     */
    void forget();

    /**
     * @brief Adds credential to YubiKey
     * @param name Full credential name (issuer:account)
     * @param secret Base32-encoded secret key
     * @param type Credential type ("TOTP" or "HOTP")
     * @param algorithm Hash algorithm ("SHA1", "SHA256", or "SHA512")
     * @param digits Number of digits (6-8)
     * @param period TOTP period in seconds (default 30)
     * @param counter Initial HOTP counter value (ignored for TOTP)
     * @param requireTouch Whether to require physical touch
     * @return AddCredentialResult with (status, pathOrMessage)
     *
     * Synchronous D-Bus call to AddCredential().
     * On success, credentialAdded signal will be emitted.
     */
    AddCredentialResult addCredential(const QString &name,
                                      const QString &secret,
                                      const QString &type,
                                      const QString &algorithm,
                                      int digits,
                                      int period,
                                      int counter,
                                      bool requireTouch);

    /**
     * @brief Sets device name
     * @param newName New friendly name for device
     * @return true on success, false on failure
     *
     * Updates D-Bus property Name.
     * Emits nameChanged signal on success.
     */
    bool setName(const QString &newName);

    // ========== Value Type Conversion ==========

    /**
     * @brief Converts to DeviceInfo value type
     * @param session Optional session proxy for populating session properties (state, hasValidPassword, lastSeen)
     * @return DeviceInfo structure for D-Bus marshaling or display
     */
    DeviceInfo toDeviceInfo(const OathDeviceSessionProxy *session = nullptr) const;

Q_SIGNALS:
    /**
     * @brief Emitted when device name changes
     * @param newName New device name
     */
    void nameChanged(const QString &newName);

    /**
     * @brief Emitted when credential is added
     * @param credential New credential proxy
     */
    void credentialAdded(OathCredentialProxy *credential);

    /**
     * @brief Emitted when credential is removed
     * @param credentialName Full credential name
     */
    void credentialRemoved(const QString &credentialName);

    /**
     * @brief Emitted when requiresPassword property changes
     * @param requiresPassword New requiresPassword state
     */
    void requiresPasswordChanged(bool requiresPassword);

    // Note: hasValidPasswordChanged(), stateChanged(), stateMessageChanged() moved to OathDeviceSessionProxy

private Q_SLOTS:
    void onCredentialAddedSignal(const QDBusObjectPath &credentialPath);
    void onCredentialRemovedSignal(const QDBusObjectPath &credentialPath);
    void onPropertiesChanged(const QString &interfaceName,
                            const QVariantMap &changedProperties,
                            const QStringList &invalidatedProperties);

public:
    /**
     * @brief Adds a credential proxy from InterfacesAdded signal
     * @param objectPath D-Bus object path of the credential
     * @param properties Credential properties from D-Bus
     *
     * Called by OathManagerProxy when InterfacesAdded signal arrives for a credential.
     * Also called internally from D-Bus CredentialAdded signal handler.
     */
    void addCredentialProxy(const QString &objectPath, const QVariantMap &properties);

private:  // NOLINT(readability-redundant-access-specifiers) - Required to close Q_SLOTS section for moc
    void connectToSignals();
    void removeCredentialProxy(const QString &objectPath);

    QString m_objectPath;
    QDBusInterface *m_interface;

    // Cached properties
    QString m_deviceId; // const - hex device ID from D-Bus
    QString m_name; // writable
    Version m_firmwareVersion; // const
    quint32 m_serialNumber{0}; // const
    QString m_deviceModel; // const - human-readable model string
    YubiKeyModel m_deviceModelCode{0}; // const - numeric model code (0xSSVVPPFF)
    QString m_formFactor; // const - human-readable form factor string
    QStringList m_capabilities; // const - list of capability strings
    bool m_requiresPassword;
    // Note: m_hasValidPassword, m_lastSeen, m_state, m_stateMessage moved to OathDeviceSessionProxy

    // Credential proxies (owned by this object via Qt parent-child)
    QHash<QString, OathCredentialProxy*> m_credentials; // key: credential name

    static constexpr const char *SERVICE_NAME = "pl.jkolo.yubikey.oath.daemon";
    static constexpr const char *INTERFACE_NAME = "pl.jkolo.yubikey.oath.Device";
    static constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
};

} // namespace Shared
} // namespace YubiKeyOath

#endif // OATH_DEVICE_PROXY_H
