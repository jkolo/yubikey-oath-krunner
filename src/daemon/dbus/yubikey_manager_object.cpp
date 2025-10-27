/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_manager_object.h"
#include "yubikey_device_object.h"
#include "services/yubikey_service.h"
#include "logging_categories.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>

namespace YubiKeyOath {
namespace Daemon {

static constexpr const char *MANAGER_PATH = "/pl/jkolo/yubikey/oath";
static constexpr const char *MANAGER_INTERFACE = "pl.jkolo.yubikey.oath.Manager";
static constexpr const char *OBJECTMANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
static constexpr const char *DAEMON_VERSION = "1.0"; // Initial release version

YubiKeyManagerObject::YubiKeyManagerObject(YubiKeyService *service,
                                           const QDBusConnection &connection,
                                           QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_connection(connection)
    , m_objectPath(QString::fromLatin1(MANAGER_PATH))
    , m_registered(false)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Constructing at path:" << m_objectPath;

    // Register D-Bus meta types for GetManagedObjects return value
    qDBusRegisterMetaType<InterfacePropertiesMap>();
    qDBusRegisterMetaType<ManagedObjectMap>();

    // Connect to service signals to track device changes
    connect(m_service, &YubiKeyService::deviceConnected,
            this, &YubiKeyManagerObject::addDevice);
    connect(m_service, &YubiKeyService::deviceDisconnected,
            this, &YubiKeyManagerObject::onDeviceDisconnected);
    connect(m_service, &YubiKeyService::deviceForgotten,
            this, &YubiKeyManagerObject::removeDevice);
}

YubiKeyManagerObject::~YubiKeyManagerObject()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Destructor";
    unregisterObject();
}

bool YubiKeyManagerObject::registerObject()
{
    if (m_registered) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyManagerObject: Already registered";
        return true;
    }

    // Register on D-Bus
    if (!m_connection.registerObject(m_objectPath, this,
                                     QDBusConnection::ExportAllProperties |
                                     QDBusConnection::ExportAllSlots |
                                     QDBusConnection::ExportAllSignals)) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyManagerObject: Failed to register object at"
                                     << m_objectPath << ":" << m_connection.lastError().message();
        return false;
    }

    m_registered = true;
    qCInfo(YubiKeyDaemonLog) << "YubiKeyManagerObject: Registered successfully at" << m_objectPath;

    return true;
}

void YubiKeyManagerObject::unregisterObject()
{
    if (!m_registered) {
        return;
    }

    // Remove all device objects first
    const QStringList deviceIds = m_devices.keys();
    for (const QString &deviceId : deviceIds) {
        removeDevice(deviceId);
    }

    m_connection.unregisterObject(m_objectPath);
    m_registered = false;
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Unregistered from" << m_objectPath;
}

QString YubiKeyManagerObject::version() const
{
    return QString::fromLatin1(DAEMON_VERSION);
}

ManagedObjectMap YubiKeyManagerObject::GetManagedObjects()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: GetManagedObjects() called";

    ManagedObjectMap result;

    // Iterate over all device objects
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        YubiKeyDeviceObject *deviceObj = it.value();

        // Get device object path and properties
        const QString devicePath = deviceObj->objectPath();
        QVariantMap deviceInterfacesVariant = deviceObj->getManagedObjectData();

        // Convert QVariantMap to InterfacePropertiesMap (QMap<QString, QVariantMap>)
        InterfacePropertiesMap deviceInterfaces;
        for (auto it = deviceInterfacesVariant.constBegin(); it != deviceInterfacesVariant.constEnd(); ++it) {
            deviceInterfaces.insert(it.key(), it.value().toMap());
        }

        qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Adding device path:" << devicePath
                                  << "interfaces:" << deviceInterfaces.keys();

        // Add device to result - convert QString to QDBusObjectPath
        result.insert(QDBusObjectPath(devicePath), deviceInterfaces);

        // Get all credential objects for this device
        const QVariantMap credentialObjects = deviceObj->getManagedCredentialObjects();
        qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device has" << credentialObjects.size() << "credentials";

        for (auto credIt = credentialObjects.constBegin();
             credIt != credentialObjects.constEnd(); ++credIt) {
            QVariantMap credInterfacesVariant = credIt.value().toMap();

            // Convert QVariantMap to InterfacePropertiesMap
            InterfacePropertiesMap credInterfaces;
            for (auto it = credInterfacesVariant.constBegin(); it != credInterfacesVariant.constEnd(); ++it) {
                credInterfaces.insert(it.key(), it.value().toMap());
            }

            qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Adding credential path:" << credIt.key()
                                      << "interfaces:" << credInterfaces.keys();
            // Convert QString to QDBusObjectPath
            result.insert(QDBusObjectPath(credIt.key()), credInterfaces);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: GetManagedObjects() returning"
                              << result.size() << "objects";

    // Debug: print all object paths
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        qCDebug(YubiKeyDaemonLog) << "  Object:" << it.key().path();
    }

    return result;
}

YubiKeyDeviceObject* YubiKeyManagerObject::addDevice(const QString &deviceId)
{
    // Delegate to addDeviceWithStatus with isConnected=true
    return addDeviceWithStatus(deviceId, true);
}

YubiKeyDeviceObject* YubiKeyManagerObject::addDeviceWithStatus(const QString &deviceId, bool isConnected)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Adding device:" << deviceId
                              << "isConnected:" << isConnected;

    // Check if already exists (might be disconnected)
    if (m_devices.contains(deviceId)) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device already exists, updating connection status:" << deviceId;
        YubiKeyDeviceObject *deviceObj = m_devices.value(deviceId);
        const bool wasConnected = deviceObj->isConnected();
        deviceObj->setConnected(isConnected);

        // Update credentials if device is being reconnected
        if (isConnected) {
            deviceObj->updateCredentials();

            // If device was disconnected and is now reconnecting, emit InterfacesAdded
            // so that clients (YubiKeyManagerProxy) can discover it again
            if (!wasConnected) {
                qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device reconnected, emitting InterfacesAdded:" << deviceId;

                const QString path = deviceObj->objectPath();
                const QDBusObjectPath dbusPath(path);
                const QVariantMap interfacesAndProperties = deviceObj->getManagedObjectData();
                Q_EMIT InterfacesAdded(dbusPath, interfacesAndProperties);

                // Also emit InterfacesAdded for all credential objects
                const QVariantMap credentialObjects = deviceObj->getManagedCredentialObjects();
                for (auto credIt = credentialObjects.constBegin();
                     credIt != credentialObjects.constEnd(); ++credIt) {
                    const QDBusObjectPath credDbusPath(credIt.key());
                    const QVariantMap credInterfacesAndProperties = credIt.value().toMap();
                    Q_EMIT InterfacesAdded(credDbusPath, credInterfacesAndProperties);
                }

                qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Emitted InterfacesAdded for device and"
                                          << credentialObjects.size() << "credentials";
            }
        }

        return deviceObj;
    }

    // Create device object with specified connection status
    const QString path = devicePath(deviceId);
    auto *deviceObj = new YubiKeyDeviceObject(deviceId, m_service, m_connection, isConnected, this);

    if (!deviceObj->registerObject()) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyManagerObject: Failed to register device object"
                                     << deviceId;
        delete deviceObj;
        return nullptr;
    }

    m_devices.insert(deviceId, deviceObj);

    // Emit ObjectManager signal: InterfacesAdded
    const QDBusObjectPath dbusPath(path);
    const QVariantMap interfacesAndProperties = deviceObj->getManagedObjectData();
    Q_EMIT InterfacesAdded(dbusPath, interfacesAndProperties);

    qCInfo(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device added successfully:" << deviceId
                             << "at" << path;

    return deviceObj;
}

void YubiKeyManagerObject::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device disconnected:" << deviceId;

    YubiKeyDeviceObject *deviceObj = m_devices.value(deviceId, nullptr);
    if (!deviceObj) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device not found:" << deviceId;
        return;
    }

    // Update connection status (keeps object on D-Bus)
    deviceObj->setConnected(false);

    // Clear credentials for disconnected device
    // updateCredentials() will fetch credentials from service, which returns empty list for disconnected devices
    // This will automatically remove all credential objects and emit InterfacesRemoved signals
    deviceObj->updateCredentials();

    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device marked as disconnected and credentials cleared:" << deviceId;
}

void YubiKeyManagerObject::removeDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyManagerObject: Removing device:" << deviceId;

    if (!m_devices.contains(deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device not found:" << deviceId;
        return;
    }

    YubiKeyDeviceObject *deviceObj = m_devices.value(deviceId);
    const QString path = deviceObj->objectPath();

    // Collect all interfaces (device + credential objects)
    QStringList interfaces;
    interfaces << QString::fromLatin1("pl.jkolo.yubikey.oath.Device");
    interfaces << QString::fromLatin1("org.freedesktop.DBus.Properties");
    interfaces << QString::fromLatin1("org.freedesktop.DBus.Introspectable");

    // Get all credential paths before deleting
    const QStringList credentialPaths = deviceObj->credentialPaths();

    // Emit InterfacesRemoved for each credential first
    for (const QString &credPath : credentialPaths) {
        const QDBusObjectPath credDbusPath(credPath);
        QStringList credInterfaces;
        credInterfaces << QString::fromLatin1("pl.jkolo.yubikey.oath.Credential");
        credInterfaces << QString::fromLatin1("org.freedesktop.DBus.Properties");
        credInterfaces << QString::fromLatin1("org.freedesktop.DBus.Introspectable");
        Q_EMIT InterfacesRemoved(credDbusPath, credInterfaces);
    }

    // Unregister and delete device object (also unregisters all credentials)
    deviceObj->unregisterObject();
    delete deviceObj;

    m_devices.remove(deviceId);

    // Emit ObjectManager signal: InterfacesRemoved for device
    const QDBusObjectPath dbusPath(path);
    Q_EMIT InterfacesRemoved(dbusPath, interfaces);

    qCInfo(YubiKeyDaemonLog) << "YubiKeyManagerObject: Device removed successfully:" << deviceId;
}

YubiKeyDeviceObject* YubiKeyManagerObject::getDevice(const QString &deviceId) const
{
    return m_devices.value(deviceId, nullptr);
}


QString YubiKeyManagerObject::devicePath(const QString &deviceId)
{
    return QString::fromLatin1("/pl/jkolo/yubikey/oath/devices/%1").arg(deviceId);
}

} // namespace Daemon
} // namespace YubiKeyOath
