/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QDBusObjectPath>
#include <QDBusConnection>
#include <QMap>
#include <QVariant>
#include "types/oath_credential.h"
#include "types/yubikey_value_types.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class YubiKeyService;
class YubiKeyCredentialObject;

/**
 * @brief Device D-Bus object for individual YubiKey
 *
 * D-Bus path: /pl/jkolo/yubikey/oath/devices/<deviceId>
 * Interfaces: pl.jkolo.yubikey.oath.Device, Properties, Introspectable
 *
 * Represents a single YubiKey device with its methods and properties.
 * Creates and manages Credential objects for OATH credentials on this device.
 *
 * @par Lifetime
 * Created when YubiKey is connected, destroyed when disconnected.
 * Owned by YubiKeyManagerObject.
 */
class YubiKeyDeviceObject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Device")

    // Properties exposed via D-Bus Properties interface
    Q_PROPERTY(QString Name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString DeviceId READ deviceId CONSTANT)
    Q_PROPERTY(bool IsConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(bool RequiresPassword READ requiresPassword NOTIFY requiresPasswordChanged)
    Q_PROPERTY(bool HasValidPassword READ hasValidPassword NOTIFY hasValidPasswordChanged)
    // Note: CredentialCount and Credentials properties removed - use Manager's GetManagedObjects() instead

public:
    /**
     * @brief Constructs Device object
     * @param deviceId Device ID (hex string)
     * @param service Pointer to YubiKeyService
     * @param connection D-Bus connection
     * @param isConnected Initial connection status
     * @param parent Parent QObject
     */
    explicit YubiKeyDeviceObject(const QString &deviceId,
                                  YubiKeyService *service,
                                  const QDBusConnection &connection,
                                  bool isConnected,
                                  QObject *parent = nullptr);
    ~YubiKeyDeviceObject() override;

    /**
     * @brief Registers this object on D-Bus
     * @return true on success
     */
    bool registerObject();

    /**
     * @brief Unregisters this object from D-Bus
     */
    void unregisterObject();

    /**
     * @brief Gets D-Bus object path
     * @return /pl/jkolo/yubikey/oath/devices/<deviceId>
     */
    QString objectPath() const;

    // Property getters
    QString name() const;
    QString deviceId() const;
    bool isConnected() const;
    bool requiresPassword() const;
    bool hasValidPassword() const;

    // Property setters
    void setName(const QString &name);
    void setConnected(bool connected);

public Q_SLOTS:
    /**
     * @brief Saves password for this device
     * @param password Password to save
     * @return true if valid and saved
     *
     * Tests password first, then saves to KWallet if valid.
     */
    bool SavePassword(const QString &password);

    /**
     * @brief Changes password on YubiKey
     * @param oldPassword Current password
     * @param newPassword New password
     * @return true on success
     */
    bool ChangePassword(const QString &oldPassword, const QString &newPassword);

    /**
     * @brief Forgets device - removes from database and KWallet
     */
    void Forget();

    /**
     * @brief Adds OATH credential to YubiKey
     * @param name Credential name
     * @param secret Base32-encoded secret
     * @param type "TOTP" or "HOTP"
     * @param algorithm "SHA1", "SHA256", "SHA512"
     * @param digits 6-8
     * @param period TOTP period (seconds)
     * @param counter HOTP counter
     * @param requireTouch Require physical touch
     * @return (status, pathOrMessage)
     *
     * Status: "Success" | "Interactive" | "Error"
     */
    Shared::AddCredentialResult AddCredential(const QString &name,
                                              const QString &secret,
                                              const QString &type,
                                              const QString &algorithm,
                                              int digits,
                                              int period,
                                              int counter,
                                              bool requireTouch);

Q_SIGNALS:
    // Property change signals
    void nameChanged(const QString &name);
    void isConnectedChanged(bool connected);
    void requiresPasswordChanged(bool required);
    void hasValidPasswordChanged(bool hasValid);

    // Device-specific signals
    void CredentialAdded(const QDBusObjectPath &credentialPath);
    void CredentialRemoved(const QDBusObjectPath &credentialPath);

    // Internal signals for Manager
    void credentialAdded();
    void credentialRemoved();

public:
    /**
     * @brief Creates and registers a Credential object
     * @param credential OathCredential data
     * @return Pointer to created CredentialObject (owned by this device)
     */
    YubiKeyCredentialObject* addCredential(const Shared::OathCredential &credential);

    /**
     * @brief Removes and unregisters a Credential object
     * @param credentialId Credential ID (encoded name)
     */
    void removeCredential(const QString &credentialId);

    /**
     * @brief Gets Credential object by ID
     * @param credentialId Credential ID
     * @return Pointer to CredentialObject or nullptr
     */
    YubiKeyCredentialObject* getCredential(const QString &credentialId) const;

    /**
     * @brief Gets all credential object paths
     * @return List of credential paths
     */
    QStringList credentialPaths() const;

    /**
     * @brief Updates credentials from service
     *
     * Called when credentials are refreshed.
     * Creates/removes credential objects as needed.
     * NOTE: Does NOT update cached properties (removed for minimalist architecture).
     */
    void updateCredentials();

    /**
     * @brief Gets ObjectManager data for this device
     * @return Map of interface → properties
     *
     * Used by Manager's GetManagedObjects()
     */
    QVariantMap getManagedObjectData() const;

    /**
     * @brief Gets all credential objects as ObjectManager data
     * @return Map of path → (interface → properties)
     *
     * Used by Manager's GetManagedObjects()
     */
    QVariantMap getManagedCredentialObjects() const;

private:
    /**
     * @brief Builds credential ID from name (encoded for D-Bus path)
     * @param credentialName Full credential name
     * @return Encoded ID suitable for object path
     */
    static QString encodeCredentialId(const QString &credentialName);

    /**
     * @brief Builds credential object path
     * @param credentialId Encoded credential ID
     * @return Full D-Bus object path
     */
    QString credentialPath(const QString &credentialId) const;

    /**
     * @brief Emits D-Bus PropertiesChanged signal
     * @param propertyName Name of changed property
     * @param value New value
     */
    void emitPropertyChanged(const QString &propertyName, const QVariant &value);

    QString m_deviceId;                                 ///< Device ID
    YubiKeyService *m_service;                          ///< Business logic service (not owned)
    QDBusConnection m_connection;                       ///< D-Bus connection
    QString m_objectPath;                               ///< Our object path
    bool m_registered;                                  ///< Registration state

    QMap<QString, YubiKeyCredentialObject*> m_credentials;  ///< Credential ID → CredentialObject

    // Cached properties
    QString m_name;
    bool m_isConnected;
    bool m_requiresPassword;
    bool m_hasValidPassword;
};

} // namespace Daemon
} // namespace YubiKeyOath
