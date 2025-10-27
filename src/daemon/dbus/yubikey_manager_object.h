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
#include <memory>

// Type for GetManagedObjects (must be outside namespace for Q_DECLARE_METATYPE)
// Signature: a{oa{sa{sv}}} = QMap<ObjectPath, QMap<InterfaceName, Properties>>
typedef QMap<QString, QVariantMap> InterfacePropertiesMap;
typedef QMap<QDBusObjectPath, InterfacePropertiesMap> ManagedObjectMap;
Q_DECLARE_METATYPE(InterfacePropertiesMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class YubiKeyDeviceObject;
class YubiKeyService;

/**
 * @brief Manager D-Bus object for YubiKey OATH daemon
 *
 * D-Bus path: /pl/jkolo/yubikey/oath
 * Interfaces: pl.jkolo.yubikey.oath.Manager, ObjectManager, Properties, Introspectable
 *
 * This is the root object in the D-Bus hierarchy that:
 * - Implements ObjectManager pattern for discovering devices and credentials
 * - Provides ONLY Version property (minimalist design per D-Bus best practices)
 * - Device/credential information obtained via GetManagedObjects()
 * - Creates/destroys Device objects dynamically
 * - Emits InterfacesAdded/InterfacesRemoved signals
 *
 * Following D-Bus best practices, this manager does NOT expose aggregated properties
 * like DeviceCount or TotalCredentials. Clients should use GetManagedObjects() to
 * discover the object hierarchy and calculate such aggregates locally if needed.
 *
 * @par Architecture
 * ```
 * YubiKeyManagerObject (/pl/jkolo/yubikey/oath)
 *     ↓ owns
 * YubiKeyDeviceObjects (/pl/jkolo/yubikey/oath/devices/<deviceId>)
 *     ↓ own
 * YubiKeyCredentialObjects (/pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>)
 * ```
 */
class YubiKeyManagerObject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Manager")
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

    // Properties exposed via D-Bus Properties interface
    // Note: Only Version is exposed. Device/credential info is obtained via GetManagedObjects()
    // following D-Bus ObjectManager best practices (no redundant aggregated properties)
    Q_PROPERTY(QString Version READ version CONSTANT)

public:
    /**
     * @brief Constructs Manager object
     * @param service Pointer to YubiKeyService (business logic layer)
     * @param connection D-Bus connection to register on
     * @param parent Parent QObject
     */
    explicit YubiKeyManagerObject(YubiKeyService *service,
                                   const QDBusConnection &connection,
                                   QObject *parent = nullptr);
    ~YubiKeyManagerObject() override;

    /**
     * @brief Registers this object on D-Bus
     * @return true on success, false on failure
     *
     * Registers object at path /pl/jkolo/yubikey/oath with all interfaces
     */
    bool registerObject();

    /**
     * @brief Unregisters this object from D-Bus
     */
    void unregisterObject();

    // Property getter
    QString version() const;

public Q_SLOTS:
    /**
     * @brief ObjectManager: Get all managed objects
     * @return Map of object paths to their interfaces and properties
     *
     * D-Bus signature: a{oa{sa{sv}}}
     * Returns the entire object hierarchy: devices + credentials
     */
    ManagedObjectMap GetManagedObjects();

Q_SIGNALS:
    // ObjectManager signals - provide device/credential discovery
    // Clients should use these signals + GetManagedObjects() instead of aggregated properties
    void InterfacesAdded(const QDBusObjectPath &object_path,
                        const QVariantMap &interfaces_and_properties);
    void InterfacesRemoved(const QDBusObjectPath &object_path,
                          const QStringList &interfaces);

public:
    /**
     * @brief Creates and registers a Device object (assumes connected=true)
     * @param deviceId Device ID
     * @return Pointer to created DeviceObject (owned by this manager)
     *
     * Called when YubiKey is connected.
     * Emits InterfacesAdded signal.
     */
    YubiKeyDeviceObject* addDevice(const QString &deviceId);

    /**
     * @brief Creates and registers a Device object with specific connection status
     * @param deviceId Device ID
     * @param isConnected Connection status
     * @return Pointer to created DeviceObject (owned by this manager)
     *
     * Used during initialization to restore devices from database.
     * Emits InterfacesAdded signal.
     */
    YubiKeyDeviceObject* addDeviceWithStatus(const QString &deviceId, bool isConnected);

    /**
     * @brief Updates device connection status
     * @param deviceId Device ID
     *
     * Called when YubiKey is physically disconnected.
     * Updates IsConnected property to false.
     * Emits PropertiesChanged signal.
     */
    void onDeviceDisconnected(const QString &deviceId);

    /**
     * @brief Removes and unregisters a Device object
     * @param deviceId Device ID
     *
     * Called when YubiKey is forgotten (removed from config).
     * Also removes all credential objects for this device.
     * Emits InterfacesRemoved signal.
     */
    void removeDevice(const QString &deviceId);

    /**
     * @brief Gets Device object by ID
     * @param deviceId Device ID
     * @return Pointer to DeviceObject or nullptr if not found
     */
    YubiKeyDeviceObject* getDevice(const QString &deviceId) const;

private:
    /**
     * @brief Builds D-Bus object path for device
     * @param deviceId Device ID
     * @return /pl/jkolo/yubikey/oath/devices/<deviceId>
     */
    static QString devicePath(const QString &deviceId);

    YubiKeyService *m_service;                          ///< Business logic service (not owned)
    QDBusConnection m_connection;                       ///< D-Bus connection
    QString m_objectPath;                               ///< Our object path
    bool m_registered;                                  ///< Registration state

    QMap<QString, YubiKeyDeviceObject*> m_devices;     ///< Device ID → DeviceObject (owned)
};

} // namespace Daemon
} // namespace YubiKeyOath
