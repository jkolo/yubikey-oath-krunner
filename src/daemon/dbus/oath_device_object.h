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
#include "shared/types/yubikey_model.h"
#include "shared/utils/version.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathService;
class OathCredentialObject;

/**
 * @brief Device D-Bus object for individual YubiKey
 *
 * D-Bus path: /pl/jkolo/yubikey/oath/devices/<deviceId>
 * Interfaces: pl.jkolo.yubikey.oath.Device, pl.jkolo.yubikey.oath.DeviceSession, Properties, Introspectable
 *
 * Represents a single YubiKey device with its methods and properties.
 * Creates and manages Credential objects for OATH credentials on this device.
 *
 * @par Two-Interface Architecture
 * This object exposes TWO D-Bus interfaces on the SAME object path:
 *
 * 1. **pl.jkolo.yubikey.oath.Device** (DeviceAdaptor.xml)
 *    - Hardware and OATH application properties (Name, FirmwareVersion, SerialNumber, DeviceModel, RequiresPassword)
 *    - OATH operations (ChangePassword, Forget, AddCredential)
 *    - Properties are STABLE across device connections
 *
 * 2. **pl.jkolo.yubikey.oath.DeviceSession** (DeviceSessionAdaptor.xml)
 *    - Runtime session state (State, StateMessage, HasValidPassword, LastSeen)
 *    - Session operations (SavePassword)
 *    - Properties are VOLATILE and change during device lifecycle
 *
 * @par PropertiesChanged Signals
 * When emitting D-Bus PropertiesChanged signals, use the correct interface name:
 * - emitDevicePropertyChanged() for Device interface properties
 * - emitSessionPropertyChanged() for DeviceSession interface properties
 * Wrong interface name will cause client proxies to NEVER receive updates!
 *
 * @par Lifetime
 * Created when YubiKey is connected, destroyed when disconnected.
 * Owned by YubiKeyManagerObject.
 */
class OathDeviceObject : public QObject
{
    Q_OBJECT
    // Note: D-Bus interfaces are handled by DeviceAdaptor and DeviceSessionAdaptor (auto-generated from XML)

    // Properties exposed via D-Bus Properties interface
    Q_PROPERTY(QString Name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(quint8 State READ state NOTIFY stateChanged)
    Q_PROPERTY(QString StateMessage READ stateMessage NOTIFY stateMessageChanged)
    Q_PROPERTY(bool RequiresPassword READ requiresPassword NOTIFY requiresPasswordChanged)
    Q_PROPERTY(bool HasValidPassword READ hasValidPassword NOTIFY hasValidPasswordChanged)
    Q_PROPERTY(QString FirmwareVersion READ firmwareVersionString CONSTANT)
    Q_PROPERTY(quint32 SerialNumber READ serialNumber CONSTANT)
    Q_PROPERTY(QString ID READ id CONSTANT)
    Q_PROPERTY(QString DeviceModel READ deviceModelString CONSTANT)
    Q_PROPERTY(quint32 DeviceModelCode READ deviceModelCode CONSTANT)
    Q_PROPERTY(QString FormFactor READ formFactorString CONSTANT)
    Q_PROPERTY(QStringList Capabilities READ capabilitiesList CONSTANT)
    Q_PROPERTY(qint64 LastSeen READ lastSeen NOTIFY lastSeenChanged)
    // Note: CredentialCount and Credentials properties removed - use Manager's GetManagedObjects() instead

public:
    /**
     * @brief Constructs Device object
     * @param deviceId Device ID (hex string)
     * @param objectPath D-Bus object path (e.g., /pl/jkolo/yubikey/oath/devices/20252879)
     * @param service Pointer to OathService
     * @param connection D-Bus connection
     * @param parent Parent QObject
     */
    explicit OathDeviceObject(QString deviceId,
                               QString objectPath,
                               OathService *service,
                               QDBusConnection connection,
                               QObject *parent = nullptr);
    ~OathDeviceObject() override;

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
     * @return /pl/jkolo/yubikey/oath/devices/<serialNumber> or /pl/jkolo/yubikey/oath/devices/dev_<deviceId>
     */
    QString objectPath() const;

    // Property getters
    QString name() const;
    quint8 state() const;
    QString stateMessage() const;
    bool requiresPassword() const;
    bool hasValidPassword() const;
    QString firmwareVersionString() const;
    quint32 serialNumber() const;
    QString id() const;
    QString deviceModelString() const;
    quint32 deviceModelCode() const;
    QString formFactorString() const;
    QStringList capabilitiesList() const;
    qint64 lastSeen() const;

    // Internal getters for raw values (used internally, not exposed via D-Bus)
    QString deviceId() const;
    Shared::YubiKeyModel deviceModel() const;
    quint8 formFactor() const;

    // Property setters
    void setName(const QString &name);
    void setState(quint8 state, const QString &message = QString());

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
    void stateChanged(quint8 newState);
    void stateMessageChanged(const QString &message);
    void requiresPasswordChanged(bool required);
    void hasValidPasswordChanged(bool hasValid);
    void lastSeenChanged(qint64 timestamp);

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
    OathCredentialObject* addCredential(const Shared::OathCredential &credential);

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
    OathCredentialObject* getCredential(const QString &credentialId) const;

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
     * @brief Connects to device and updates state
     *
     * Called when device becomes available (after connectToDeviceAsync completes).
     * Connects to device state signals and updates current state.
     */
    void connectToDevice();

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
     * @param interfaceName D-Bus interface name (e.g., "pl.jkolo.yubikey.oath.Device")
     * @param propertyName Name of changed property
     * @param value New value
     */
    void emitPropertyChanged(const QString &interfaceName,
                            const QString &propertyName,
                            const QVariant &value);

    /**
     * @brief Helper: Emits D-Bus PropertiesChanged for Device interface property
     * @param propertyName Name of changed property (e.g., "Name", "RequiresPassword")
     * @param value New value
     */
    void emitDevicePropertyChanged(const QString &propertyName, const QVariant &value);

    /**
     * @brief Helper: Emits D-Bus PropertiesChanged for DeviceSession interface property
     * @param propertyName Name of changed property (e.g., "State", "HasValidPassword")
     * @param value New value
     */
    void emitSessionPropertyChanged(const QString &propertyName, const QVariant &value);

    QString m_deviceId;                                 ///< Device ID
    OathService *m_service;                          ///< Business logic service (not owned)
    QDBusConnection m_connection;                       ///< D-Bus connection
    QString m_objectPath;                               ///< Our object path
    QString m_id;                                       ///< Public ID (last segment of path: serialNumber or dev_<deviceId>)
    bool m_registered;                                  ///< Registration state

    QMap<QString, OathCredentialObject*> m_credentials;  ///< Credential ID → CredentialObject

    // Cached properties
    QString m_name;
    quint8 m_state{0x00};  // DeviceState::Disconnected
    QString m_stateMessage;
    bool m_requiresPassword;
    bool m_hasValidPassword;
    Shared::Version m_firmwareVersion;
    quint32 m_serialNumber{0};
    QString m_deviceModel;        ///< Human-readable model string
    QString m_formFactor;          ///< Human-readable form factor string
    QStringList m_capabilities;    ///< List of capability strings

    // Raw values kept for internal use (e.g., building object paths, logic)
    Shared::YubiKeyModel m_rawDeviceModel{0x00000000};
    quint8 m_rawFormFactor{0};
};

} // namespace Daemon
} // namespace YubiKeyOath
