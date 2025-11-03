/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_manager_proxy.h"
#include "../../daemon/dbus/yubikey_manager_object.h"  // For ManagedObjectMap
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QLatin1String>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(YubiKeyManagerProxyLog, "pl.jkolo.yubikey.oath.daemon.manager.proxy")

namespace YubiKeyOath {
namespace Shared {

// Initialize singleton
YubiKeyManagerProxy *YubiKeyManagerProxy::s_instance = nullptr;

// Register D-Bus types on first use
static void registerDBusTypes()
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<ManagedObjectMap>();
        qDBusRegisterMetaType<InterfacePropertiesMap>();
        registered = true;
    }
}

YubiKeyManagerProxy* YubiKeyManagerProxy::instance(QObject *parent)
{
    if (!s_instance) {
        s_instance = new YubiKeyManagerProxy(parent);
    }
    return s_instance;
}

YubiKeyManagerProxy::YubiKeyManagerProxy(QObject *parent)
    : QObject(parent)
    , m_version(QStringLiteral("2.0.0"))
{
    // Register D-Bus types
    registerDBusTypes();

    qCDebug(YubiKeyManagerProxyLog) << "Creating YubiKeyManagerProxy singleton";

    // Create D-Bus interfaces
    m_managerInterface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                            QLatin1String(MANAGER_PATH),
                                            QLatin1String(MANAGER_INTERFACE),
                                            QDBusConnection::sessionBus(),
                                            this);

    m_objectManagerInterface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                                   QLatin1String(MANAGER_PATH),
                                                   QLatin1String(OBJECT_MANAGER_INTERFACE),
                                                   QDBusConnection::sessionBus(),
                                                   this);

    // Setup service watcher for daemon availability
    setupServiceWatcher();

    // Check initial daemon availability
    m_daemonAvailable = m_managerInterface->isValid() && m_objectManagerInterface->isValid();

    if (m_daemonAvailable) {
        qCDebug(YubiKeyManagerProxyLog) << "Daemon is available on startup";
        connectToSignals();
        refreshManagedObjects();
    } else {
        qCWarning(YubiKeyManagerProxyLog) << "Daemon not available on startup";
    }
}

YubiKeyManagerProxy::~YubiKeyManagerProxy()
{
    qCDebug(YubiKeyManagerProxyLog) << "Destroying YubiKeyManagerProxy singleton";
}

void YubiKeyManagerProxy::setupServiceWatcher()
{
    m_serviceWatcher = new QDBusServiceWatcher(QLatin1String(SERVICE_NAME),
                                               QDBusConnection::sessionBus(),
                                               QDBusServiceWatcher::WatchForRegistration |
                                               QDBusServiceWatcher::WatchForUnregistration,
                                               this);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &YubiKeyManagerProxy::onDBusServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &YubiKeyManagerProxy::onDBusServiceUnregistered);
}

void YubiKeyManagerProxy::connectToSignals()
{
    if (!m_objectManagerInterface || !m_objectManagerInterface->isValid()) {
        return;
    }

    // Connect to ObjectManager signals
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        QLatin1String(MANAGER_PATH),
        QLatin1String(OBJECT_MANAGER_INTERFACE),
        QStringLiteral("InterfacesAdded"),
        this,
        SLOT(onInterfacesAdded(QDBusObjectPath,QVariantMap))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        QLatin1String(MANAGER_PATH),
        QLatin1String(OBJECT_MANAGER_INTERFACE),
        QStringLiteral("InterfacesRemoved"),
        this,
        SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList))
    );

    // Connect to Manager PropertiesChanged
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        QLatin1String(MANAGER_PATH),
        QLatin1String(PROPERTIES_INTERFACE),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onManagerPropertiesChanged(QString,QVariantMap,QStringList))
    );
}

void YubiKeyManagerProxy::refreshManagedObjects()
{
    if (!m_objectManagerInterface || !m_objectManagerInterface->isValid()) {
        qCWarning(YubiKeyManagerProxyLog) << "Cannot refresh: ObjectManager interface invalid";
        return;
    }

    qCDebug(YubiKeyManagerProxyLog) << "Calling GetManagedObjects()";

    // Call GetManagedObjects()
    // Returns: a{oa{sa{sv}}} - ObjectManager signature
    // Qt returns this as QDBusArgument which needs manual demarshalling
    QDBusMessage const reply = m_objectManagerInterface->call(QStringLiteral("GetManagedObjects"));

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(YubiKeyManagerProxyLog) << "GetManagedObjects failed:"
                                           << reply.errorMessage();
        return;
    }

    // Use qdbus_cast to handle complex nested D-Bus structure
    // Signature: a{oa{sa{sv}}}
    // Full type: QMap<QDBusObjectPath, QMap<QString, QVariantMap>>
    //   Level 1: object path → interfaces map
    //   Level 2: interface name → properties map
    //   Level 3: property name → value

    auto const arg = reply.arguments().at(0).value<QDBusArgument>();

    // Use qdbus_cast with correct 3-level nested type
    using InterfacePropertiesMap = QMap<QString, QVariantMap>;
    using DBusObjectMap = QMap<QDBusObjectPath, InterfacePropertiesMap>;
    auto const dbusObjects = qdbus_cast<DBusObjectMap>(arg);

    qCDebug(YubiKeyManagerProxyLog) << "qdbus_cast returned" << dbusObjects.size() << "objects";

    // Convert QDBusObjectPath keys to QString for easier handling
    QVariantMap managedObjects;
    for (auto it = dbusObjects.constBegin(); it != dbusObjects.constEnd(); ++it) {
        QString const objectPath = it.key().path();

        // it.value() is InterfacePropertiesMap = QMap<QString, QVariantMap>
        // Convert to QVariantMap for storage
        QVariantMap interfacesMap;
        const InterfacePropertiesMap &interfaces = it.value();
        for (auto intIt = interfaces.constBegin(); intIt != interfaces.constEnd(); ++intIt) {
            interfacesMap.insert(intIt.key(), QVariant::fromValue(intIt.value()));
        }

        qCDebug(YubiKeyManagerProxyLog) << "DEBUG: Object path:" << objectPath
                                         << "with" << interfacesMap.size() << "interfaces";

        managedObjects.insert(objectPath, interfacesMap);
    }

    qCDebug(YubiKeyManagerProxyLog) << "GetManagedObjects returned" << managedObjects.size() << "objects";

    // First pass: collect all device and credential objects
    QHash<QString, QVariantMap> deviceObjects; // devicePath → device properties
    QHash<QString, QHash<QString, QVariantMap>> credentialsByDevice; // devicePath → (credPath → cred properties)

    for (auto it = managedObjects.constBegin(); it != managedObjects.constEnd(); ++it) {
        const QString &objectPath = it.key();
        QVariantMap const interfacesAndProperties = it.value().toMap();

        // Check if this is a Device object
        if (interfacesAndProperties.contains(QLatin1String(DEVICE_INTERFACE))) {
            QVariantMap const deviceProps = interfacesAndProperties.value(QLatin1String(DEVICE_INTERFACE)).toMap();
            deviceObjects.insert(objectPath, deviceProps);
            qCDebug(YubiKeyManagerProxyLog) << "Found device at" << objectPath;
        }

        // Check if this is a Credential object
        if (interfacesAndProperties.contains(QLatin1String(CREDENTIAL_INTERFACE))) {
            QVariantMap const credProps = interfacesAndProperties.value(QLatin1String(CREDENTIAL_INTERFACE)).toMap();

            // Extract parent device path from credential path
            // Format: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
            QString devicePath;
            if (objectPath.contains(QLatin1String("/credentials/"))) {
                devicePath = objectPath.section(QLatin1String("/credentials/"), 0, 0);
            }

            if (!devicePath.isEmpty()) {
                credentialsByDevice[devicePath].insert(objectPath, credProps);
                qCDebug(YubiKeyManagerProxyLog) << "Found credential at" << objectPath
                                                 << "for device" << devicePath;
            }
        }
    }

    // Second pass: create device proxies with their credentials
    for (auto it = deviceObjects.constBegin(); it != deviceObjects.constEnd(); ++it) {
        const QString &devicePath = it.key();
        const QVariantMap &deviceProps = it.value();
        QHash<QString, QVariantMap> const credentials = credentialsByDevice.value(devicePath);

        addDeviceProxy(devicePath, deviceProps, credentials);
    }

    qCDebug(YubiKeyManagerProxyLog) << "Refresh complete:"
                                     << m_devices.size() << "devices,"
                                     << totalCredentials() << "credentials";
}

void YubiKeyManagerProxy::refresh()
{
    qCDebug(YubiKeyManagerProxyLog) << "Manual refresh requested";
    refreshManagedObjects();
}

QList<YubiKeyDeviceProxy*> YubiKeyManagerProxy::devices() const
{
    return m_devices.values();
}

YubiKeyDeviceProxy* YubiKeyManagerProxy::getDevice(const QString &deviceId) const
{
    return m_devices.value(deviceId, nullptr);
}

QList<YubiKeyCredentialProxy*> YubiKeyManagerProxy::getAllCredentials() const
{
    QList<YubiKeyCredentialProxy*> allCredentials;

    for (auto *device : m_devices) {
        allCredentials.append(device->credentials());
    }

    return allCredentials;
}

int YubiKeyManagerProxy::totalCredentials() const
{
    int total = 0;
    for (auto *device : m_devices) {
        total += static_cast<int>(device->credentials().size());
    }
    return total;
}

void YubiKeyManagerProxy::onInterfacesAdded(const QDBusObjectPath &objectPath,
                                           const QVariantMap &interfacesAndProperties)
{
    QString const path = objectPath.path();
    qCDebug(YubiKeyManagerProxyLog) << "InterfacesAdded:" << path;

    // Debug: log all interfaces and properties
    qCDebug(YubiKeyManagerProxyLog) << "Interfaces in signal:" << interfacesAndProperties.keys();

    // Check if this is a Device object
    if (interfacesAndProperties.contains(QLatin1String(DEVICE_INTERFACE))) {
        QVariantMap const deviceProps = interfacesAndProperties.value(QLatin1String(DEVICE_INTERFACE)).toMap();

        // Debug: log device properties
        qCDebug(YubiKeyManagerProxyLog) << "Device properties:" << deviceProps;
        qCDebug(YubiKeyManagerProxyLog) << "DeviceId property value:" << deviceProps.value(QLatin1String("DeviceId"));

        QHash<QString, QVariantMap> const emptyCredentials; // New device has no credentials yet
        addDeviceProxy(path, deviceProps, emptyCredentials);
    }

    // Credential additions are handled by DeviceProxy's CredentialAdded signal
}

void YubiKeyManagerProxy::onInterfacesRemoved(const QDBusObjectPath &objectPath,
                                             const QStringList &interfaces)
{
    QString const path = objectPath.path();
    qCDebug(YubiKeyManagerProxyLog) << "InterfacesRemoved:" << path << "Interfaces:" << interfaces;

    // Check if Device interface was removed
    if (interfaces.contains(QLatin1String(DEVICE_INTERFACE))) {
        removeDeviceProxy(path);
    }

    // Credential removals are handled by DeviceProxy's CredentialRemoved signal
}

void YubiKeyManagerProxy::onManagerPropertiesChanged(const QString &interfaceName,
                                                    const QVariantMap &changedProperties,
                                                    const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)

    if (interfaceName != QLatin1String(MANAGER_INTERFACE)) {
        return;
    }

    qCDebug(YubiKeyManagerProxyLog) << "Manager PropertiesChanged:" << changedProperties.keys();

    // Update cached manager properties
    if (changedProperties.contains(QStringLiteral("Version"))) {
        m_version = changedProperties.value(QStringLiteral("Version")).toString();
    }

    // Emit credentialsChanged if Credentials property changed
    if (changedProperties.contains(QStringLiteral("Credentials"))) {
        Q_EMIT credentialsChanged();
    }
}

void YubiKeyManagerProxy::onDBusServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qCDebug(YubiKeyManagerProxyLog) << "Daemon service registered";

    // CRITICAL FIX: Recreate D-Bus interfaces for new daemon instance
    // Old interfaces become stale after daemon crash/restart and isValid() returns false
    qCDebug(YubiKeyManagerProxyLog) << "Recreating D-Bus interfaces for new daemon instance";

    delete m_managerInterface;
    delete m_objectManagerInterface;

    m_managerInterface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                            QLatin1String(MANAGER_PATH),
                                            QLatin1String(MANAGER_INTERFACE),
                                            QDBusConnection::sessionBus(),
                                            this);

    m_objectManagerInterface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                                   QLatin1String(MANAGER_PATH),
                                                   QLatin1String(OBJECT_MANAGER_INTERFACE),
                                                   QDBusConnection::sessionBus(),
                                                   this);

    m_daemonAvailable = true;
    Q_EMIT daemonAvailable();

    // Reconnect to signals and refresh objects with new interfaces
    connectToSignals();
    refreshManagedObjects();
}

void YubiKeyManagerProxy::onDBusServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qCWarning(YubiKeyManagerProxyLog) << "Daemon service unregistered";

    m_daemonAvailable = false;
    Q_EMIT daemonUnavailable();

    // Clear all device proxies
    QStringList const deviceIds = m_devices.keys();
    for (const QString &deviceId : deviceIds) {
        removeDeviceProxy(m_devices.value(deviceId)->objectPath());
    }
}

void YubiKeyManagerProxy::addDeviceProxy(const QString &devicePath,
                                        const QVariantMap &deviceProperties,
                                        const QHash<QString, QVariantMap> &credentialObjects)
{
    // Extract device ID from properties
    QString const deviceId = deviceProperties.value(QStringLiteral("DeviceId")).toString();

    if (deviceId.isEmpty()) {
        qCWarning(YubiKeyManagerProxyLog) << "Cannot add device proxy: deviceId is empty for path" << devicePath;
        return;
    }

    // Check if already exists
    if (m_devices.contains(deviceId)) {
        qCDebug(YubiKeyManagerProxyLog) << "Device" << deviceId << "already exists, skipping";
        return;
    }

    // Create device proxy (this object becomes parent, so proxy is auto-deleted)
    auto *device = new YubiKeyDeviceProxy(devicePath, deviceProperties, credentialObjects, this);
    m_devices.insert(deviceId, device);

    // Connect to device signals for credential changes
    connect(device, &YubiKeyDeviceProxy::credentialAdded,
            this, &YubiKeyManagerProxy::credentialsChanged);
    connect(device, &YubiKeyDeviceProxy::credentialRemoved,
            this, &YubiKeyManagerProxy::credentialsChanged);

    qCDebug(YubiKeyManagerProxyLog) << "Added device proxy:" << deviceId
                                     << "Name:" << device->name()
                                     << "Credentials:" << device->credentials().size();
    Q_EMIT deviceConnected(device);
}

void YubiKeyManagerProxy::removeDeviceProxy(const QString &devicePath)
{
    // Find device by object path
    QString deviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value()->objectPath() == devicePath) {
            deviceId = it.key();
            break;
        }
    }

    if (deviceId.isEmpty()) {
        qCDebug(YubiKeyManagerProxyLog) << "Device not found for path" << devicePath;
        return;
    }

    // Remove and delete device proxy
    auto *device = m_devices.take(deviceId);
    if (device) {
        qCDebug(YubiKeyManagerProxyLog) << "Removed device proxy:" << deviceId;
        Q_EMIT deviceDisconnected(deviceId);
        device->deleteLater();
    }
}

} // namespace Shared
} // namespace YubiKeyOath
