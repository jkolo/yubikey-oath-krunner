/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_manager_proxy.h"
#include "../../daemon/dbus/oath_manager_object.h"  // For ManagedObjectMap
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLatin1String>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(OathManagerProxyLog, "pl.jkolo.yubikey.oath.daemon.manager.proxy")

namespace YubiKeyOath {
namespace Shared {

// Initialize singleton
OathManagerProxy *OathManagerProxy::s_instance = nullptr;

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

OathManagerProxy* OathManagerProxy::instance(QObject *parent)
{
    if (!s_instance) {
        s_instance = new OathManagerProxy(parent);
    }
    return s_instance;
}

OathManagerProxy::OathManagerProxy(QObject *parent)
    : QObject(parent)
    , m_version(QStringLiteral("2.0.0"))
{
    // Register D-Bus types
    registerDBusTypes();

    qCDebug(OathManagerProxyLog) << "Creating OathManagerProxy singleton";

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
        qCDebug(OathManagerProxyLog) << "Daemon is available on startup";
        connectToSignals();
        refreshManagedObjects();
    } else {
        qCWarning(OathManagerProxyLog) << "Daemon not available on startup";
    }
}

OathManagerProxy::~OathManagerProxy()
{
    qCDebug(OathManagerProxyLog) << "Destroying OathManagerProxy singleton";
}

void OathManagerProxy::setupServiceWatcher()
{
    m_serviceWatcher = new QDBusServiceWatcher(QLatin1String(SERVICE_NAME),
                                               QDBusConnection::sessionBus(),
                                               QDBusServiceWatcher::WatchForRegistration |
                                               QDBusServiceWatcher::WatchForUnregistration,
                                               this);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &OathManagerProxy::onDBusServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &OathManagerProxy::onDBusServiceUnregistered);
}

void OathManagerProxy::connectToSignals()
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
        SLOT(onInterfacesAdded(QDBusMessage))
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

void OathManagerProxy::refreshManagedObjects()
{
    if (!m_objectManagerInterface || !m_objectManagerInterface->isValid()) {
        qCWarning(OathManagerProxyLog) << "Cannot refresh: ObjectManager interface invalid";
        return;
    }

    qCDebug(OathManagerProxyLog) << "Calling GetManagedObjects() asynchronously";

    // Call GetManagedObjects() asynchronously (non-blocking)
    // Returns: a{oa{sa{sv}}} - ObjectManager signature
    QDBusPendingCall const pendingCall = m_objectManagerInterface->asyncCall(QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &OathManagerProxy::onGetManagedObjectsFinished);
}

void OathManagerProxy::onGetManagedObjectsFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();  // Clean up watcher

    QDBusPendingReply<> const reply = *watcher;
    if (reply.isError()) {
        qCWarning(OathManagerProxyLog) << "GetManagedObjects async call failed:"
                                       << reply.error().message();
        return;
    }

    qCDebug(OathManagerProxyLog) << "GetManagedObjects async reply received";

    // Use qdbus_cast to handle complex nested D-Bus structure
    // Signature: a{oa{sa{sv}}}
    // Full type: QMap<QDBusObjectPath, QMap<QString, QVariantMap>>
    //   Level 1: object path → interfaces map
    //   Level 2: interface name → properties map
    //   Level 3: property name → value

    QDBusMessage const message = reply.reply();
    auto const arg = message.arguments().at(0).value<QDBusArgument>();

    // Use qdbus_cast with correct 3-level nested type
    using InterfacePropertiesMap = QMap<QString, QVariantMap>;
    using DBusObjectMap = QMap<QDBusObjectPath, InterfacePropertiesMap>;
    auto const dbusObjects = qdbus_cast<DBusObjectMap>(arg);

    qCDebug(OathManagerProxyLog) << "qdbus_cast returned" << dbusObjects.size() << "objects";

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

        qCDebug(OathManagerProxyLog) << "DEBUG: Object path:" << objectPath
                                         << "with" << interfacesMap.size() << "interfaces";

        managedObjects.insert(objectPath, interfacesMap);
    }

    qCDebug(OathManagerProxyLog) << "GetManagedObjects returned" << managedObjects.size() << "objects";

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
            qCDebug(OathManagerProxyLog) << "Found device at" << objectPath;
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
                qCDebug(OathManagerProxyLog) << "Found credential at" << objectPath
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

    qCDebug(OathManagerProxyLog) << "Async refresh complete:"
                                     << m_devices.size() << "devices,"
                                     << totalCredentials() << "credentials";
}

void OathManagerProxy::refresh()
{
    qCDebug(OathManagerProxyLog) << "Manual refresh requested";
    refreshManagedObjects();
}

QList<OathDeviceProxy*> OathManagerProxy::devices() const
{
    return m_devices.values();
}

OathDeviceProxy* OathManagerProxy::getDevice(const QString &deviceId) const
{
    return m_devices.value(deviceId, nullptr);
}

QList<OathCredentialProxy*> OathManagerProxy::getAllCredentials() const
{
    QList<OathCredentialProxy*> allCredentials;

    for (auto *device : m_devices) {
        allCredentials.append(device->credentials());
    }

    return allCredentials;
}

int OathManagerProxy::totalCredentials() const
{
    int total = 0;
    for (auto *device : m_devices) {
        total += static_cast<int>(device->credentials().size());
    }
    return total;
}

void OathManagerProxy::onInterfacesAdded(const QDBusMessage &message)
{
    // Extract arguments from D-Bus message
    // Argument 0: object path (o)
    // Argument 1: interfaces and properties (a{sa{sv}})
    QList<QVariant> const args = message.arguments();
    if (args.size() < 2) {
        qCWarning(OathManagerProxyLog) << "InterfacesAdded: Invalid message - expected 2 arguments, got" << args.size();
        return;
    }

    auto const objectPath = args.at(0).value<QDBusObjectPath>();
    QString const path = objectPath.path();
    qCDebug(OathManagerProxyLog) << "InterfacesAdded:" << path;

    // Manual demarshaling of nested D-Bus structure
    // D-Bus signature: a{sa{sv}} = Map<interface_name, Map<property_name, value>>
    // Use qdbus_cast for type conversion, then wrap in QVariant
    using InterfacePropertiesMap = QMap<QString, QVariantMap>;
    auto const interfacesArg = args.at(1).value<QDBusArgument>();
    auto const interfaces = qdbus_cast<InterfacePropertiesMap>(interfacesArg);

    // Debug: log all interfaces
    qCDebug(OathManagerProxyLog) << "Demarshaled interfaces:" << interfaces.keys();

    // Convert to QVariantMap matching refreshManagedObjects pattern
    // Key insight: each property map must be wrapped in QVariant
    QVariantMap interfacesMap;
    for (auto intIt = interfaces.constBegin(); intIt != interfaces.constEnd(); ++intIt) {
        const QString &interfaceName = intIt.key();
        const QVariantMap &properties = intIt.value();

        // Debug: log each property in the map
        qCDebug(OathManagerProxyLog) << "Interface" << interfaceName << "properties:";
        for (auto propIt = properties.constBegin(); propIt != properties.constEnd(); ++propIt) {
            qCDebug(OathManagerProxyLog) << "  " << propIt.key() << "=" << propIt.value();
        }

        interfacesMap.insert(interfaceName, QVariant::fromValue(properties));
    }

    qCDebug(OathManagerProxyLog) << "Total interfaces wrapped:" << interfacesMap.size();

    // Check if this is a Device object
    if (interfacesMap.contains(QLatin1String(DEVICE_INTERFACE))) {
        // Extract device properties with .toMap() to unwrap QVariant (same pattern as refreshManagedObjects:205)
        QVariantMap const deviceProps = interfacesMap.value(QLatin1String(DEVICE_INTERFACE)).toMap();

        // Debug: log device properties
        qCDebug(OathManagerProxyLog) << "Device properties:" << deviceProps;
        qCDebug(OathManagerProxyLog) << "DeviceId (ID property):" << deviceProps.value(QLatin1String("ID"));

        QHash<QString, QVariantMap> const emptyCredentials; // New device has no credentials yet
        addDeviceProxy(path, deviceProps, emptyCredentials);
    }

    // Credential additions are handled by DeviceProxy's CredentialAdded signal
}

void OathManagerProxy::onInterfacesRemoved(const QDBusObjectPath &objectPath,
                                             const QStringList &interfaces)
{
    QString const path = objectPath.path();
    qCDebug(OathManagerProxyLog) << "InterfacesRemoved:" << path << "Interfaces:" << interfaces;

    // Check if Device interface was removed
    if (interfaces.contains(QLatin1String(DEVICE_INTERFACE))) {
        removeDeviceProxy(path);
    }

    // Credential removals are handled by DeviceProxy's CredentialRemoved signal
}

void OathManagerProxy::onManagerPropertiesChanged(const QString &interfaceName,
                                                    const QVariantMap &changedProperties,
                                                    const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)

    if (interfaceName != QLatin1String(MANAGER_INTERFACE)) {
        return;
    }

    qCDebug(OathManagerProxyLog) << "Manager PropertiesChanged:" << changedProperties.keys();

    // Update cached manager properties
    if (changedProperties.contains(QStringLiteral("Version"))) {
        m_version = changedProperties.value(QStringLiteral("Version")).toString();
    }

    // Emit credentialsChanged if Credentials property changed
    if (changedProperties.contains(QStringLiteral("Credentials"))) {
        Q_EMIT credentialsChanged();
    }
}

void OathManagerProxy::onDBusServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qCDebug(OathManagerProxyLog) << "Daemon service registered";

    // CRITICAL FIX: Recreate D-Bus interfaces for new daemon instance
    // Old interfaces become stale after daemon crash/restart and isValid() returns false
    qCDebug(OathManagerProxyLog) << "Recreating D-Bus interfaces for new daemon instance";

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

void OathManagerProxy::onDBusServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qCWarning(OathManagerProxyLog) << "Daemon service unregistered";

    m_daemonAvailable = false;
    Q_EMIT daemonUnavailable();

    // Clear all device proxies
    QStringList const deviceIds = m_devices.keys();
    for (const QString &deviceId : deviceIds) {
        removeDeviceProxy(m_devices.value(deviceId)->objectPath());
    }
}

void OathManagerProxy::addDeviceProxy(const QString &devicePath,
                                        const QVariantMap &deviceProperties,
                                        const QHash<QString, QVariantMap> &credentialObjects)
{
    // Extract device ID from properties (ID property contains last path segment: serialNumber or dev_<deviceId>)
    QString const deviceId = deviceProperties.value(QStringLiteral("ID")).toString();

    if (deviceId.isEmpty()) {
        qCWarning(OathManagerProxyLog) << "Cannot add device proxy: ID is empty for path" << devicePath;
        return;
    }

    // Check if already exists
    if (m_devices.contains(deviceId)) {
        qCDebug(OathManagerProxyLog) << "Device" << deviceId << "already exists, skipping";
        return;
    }

    // Create device proxy (this object becomes parent, so proxy is auto-deleted)
    auto *device = new OathDeviceProxy(devicePath, deviceProperties, credentialObjects, this);
    m_devices.insert(deviceId, device);

    // Connect to device signals for credential changes
    connect(device, &OathDeviceProxy::credentialAdded,
            this, &OathManagerProxy::credentialsChanged);
    connect(device, &OathDeviceProxy::credentialRemoved,
            this, &OathManagerProxy::credentialsChanged);

    // Forward device property changes
    connect(device, &OathDeviceProxy::nameChanged,
            this, [this, device]() { Q_EMIT devicePropertyChanged(device); });
    connect(device, &OathDeviceProxy::stateChanged,
            this, [this, device]() { Q_EMIT devicePropertyChanged(device); });
    connect(device, &OathDeviceProxy::requiresPasswordChanged,
            this, [this, device]() { Q_EMIT devicePropertyChanged(device); });
    connect(device, &OathDeviceProxy::hasValidPasswordChanged,
            this, [this, device]() { Q_EMIT devicePropertyChanged(device); });

    qCDebug(OathManagerProxyLog) << "Added device proxy:" << deviceId
                                     << "Name:" << device->name()
                                     << "Credentials:" << device->credentials().size();
    Q_EMIT deviceConnected(device);
}

void OathManagerProxy::removeDeviceProxy(const QString &devicePath)
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
        qCDebug(OathManagerProxyLog) << "Device not found for path" << devicePath;
        return;
    }

    // Remove and delete device proxy
    auto *device = m_devices.take(deviceId);
    if (device) {
        qCDebug(OathManagerProxyLog) << "Removed device proxy:" << deviceId;
        Q_EMIT deviceDisconnected(deviceId);
        device->deleteLater();
    }
}

} // namespace Shared
} // namespace YubiKeyOath
