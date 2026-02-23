/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device_object.h"
#include "credential_object_manager.h"
#include "oath_credential_object.h"
#include "services/oath_service.h"
#include "oath/oath_device.h"
#include "types/device_state.h"
#include "utils/credential_id_encoder.h"
#include "logging_categories.h"
#include "deviceadaptor.h"         // Auto-generated D-Bus adaptor for Device interface
#include "devicesessionadaptor.h"  // Auto-generated D-Bus adaptor for DeviceSession interface

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <utility>

namespace YubiKeyOath {
namespace Daemon {

static constexpr const char *DEVICE_INTERFACE = "pl.jkolo.yubikey.oath.Device";
static constexpr const char *DEVICE_SESSION_INTERFACE = "pl.jkolo.yubikey.oath.DeviceSession";

OathDeviceObject::OathDeviceObject(QString deviceId,
                                         QString objectPath,
                                         OathService *service,
                                         QDBusConnection connection,
                                         QObject *parent)
    : QObject(parent)
    , m_deviceId(std::move(deviceId))
    , m_service(service)
    , m_connection(std::move(connection))
    , m_objectPath(std::move(objectPath))
    , m_id(m_objectPath.section(QLatin1Char('/'), -1))  // Extract last segment of path
    , m_registered(false)
    , m_requiresPassword(false)
    , m_hasValidPassword(false)
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Constructing for device:" << m_deviceId
                              << "at path:" << m_objectPath;

    // Create D-Bus adaptors for Device and DeviceSession interfaces
    // These automatically register pl.jkolo.yubikey.oath.Device and pl.jkolo.yubikey.oath.DeviceSession
    new DeviceAdaptor(this);
    new DeviceSessionAdaptor(this);
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: DeviceAdaptor and DeviceSessionAdaptor created";

    // Get initial device info from service
    const auto devices = m_service->listDevices();
    for (const auto &devInfo : devices) {
        if (devInfo._internalDeviceId == m_deviceId) {
            m_name = devInfo.deviceName;
            m_requiresPassword = devInfo.requiresPassword;
            m_hasValidPassword = devInfo.hasValidPassword;
            m_firmwareVersion = devInfo.firmwareVersion;
            m_deviceModel = devInfo.deviceModel;
            m_rawDeviceModel = devInfo.deviceModelCode;  // Numeric model code
            m_serialNumber = devInfo.serialNumber;
            m_formFactor = devInfo.formFactor;
            m_capabilities = devInfo.capabilities;

            // Raw form factor needs to be obtained from daemon's internal structures
            // For now, initialize with default - it's only used for internal logic
            m_rawFormFactor = 0;
            break;
        }
    }

    // Create credential object manager
    m_credentialManager = std::make_unique<CredentialObjectManager>(
        m_deviceId, m_objectPath, m_service, m_connection, this);

    // Connect credential manager signals to D-Bus signals
    connect(m_credentialManager.get(), &CredentialObjectManager::credentialAdded,
            this, [this](const QString &path) {
                Q_EMIT CredentialAdded(QDBusObjectPath(path));
                Q_EMIT credentialAdded();
            });
    connect(m_credentialManager.get(), &CredentialObjectManager::credentialRemoved,
            this, [this](const QString &path) {
                Q_EMIT CredentialRemoved(QDBusObjectPath(path));
                Q_EMIT credentialRemoved();
            });

    // Connect to service signals for credential updates
    connect(m_service, &OathService::credentialsUpdated,
            this, [this](const QString &deviceId) {
                if (deviceId == m_deviceId) {
                    m_credentialManager->updateCredentials();
                }
            });

    // Connect to device state signals if device is available
    auto *device = m_service->getDevice(m_deviceId);
    if (device) {
        connect(device, &OathDevice::stateChanged,
                this, [this](Shared::DeviceState newState) {
                    auto *dev = m_service->getDevice(m_deviceId);
                    const QString errorMsg = dev ? dev->lastError() : QString();
                    setState(static_cast<quint8>(newState), errorMsg);
                });

        // Set initial state from device
        setState(static_cast<quint8>(device->state()), device->lastError());
    }
}

OathDeviceObject::~OathDeviceObject()
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Destructor for device:" << m_deviceId;
    unregisterObject();
}

bool OathDeviceObject::registerObject()
{
    if (m_registered) {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Already registered:" << m_deviceId;
        return true;
    }

    // Register on D-Bus using adaptor (exports interfaces defined in XML)
    // Using ExportAdaptors ensures we use the interface name from the adaptor's Q_CLASSINFO,
    // not the C++ class name
    if (!m_connection.registerObject(m_objectPath, this, QDBusConnection::ExportAdaptors)) {
        qCCritical(OathDaemonLog) << "YubiKeyDeviceObject: Failed to register at"
                                     << m_objectPath << ":" << m_connection.lastError().message();
        return false;
    }

    m_registered = true;
    qCInfo(OathDaemonLog) << "YubiKeyDeviceObject: Registered successfully:" << m_deviceId
                             << "at" << m_objectPath;

    // Load initial credentials
    updateCredentials();

    return true;
}

void OathDeviceObject::unregisterObject()
{
    if (!m_registered) {
        return;
    }

    // Remove all credential objects first (manager handles cleanup)
    m_credentialManager->removeAllCredentials();

    m_connection.unregisterObject(m_objectPath);
    m_registered = false;
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Unregistered:" << m_deviceId;
}

QString OathDeviceObject::objectPath() const
{
    return m_objectPath;
}

QString OathDeviceObject::name() const
{
    return m_name;
}

quint8 OathDeviceObject::state() const
{
    return m_state;
}

QString OathDeviceObject::stateMessage() const
{
    return m_stateMessage;
}

QString OathDeviceObject::deviceId() const
{
    return m_deviceId;
}

bool OathDeviceObject::requiresPassword() const
{
    return m_requiresPassword;
}

bool OathDeviceObject::hasValidPassword() const
{
    return m_hasValidPassword;
}

QString OathDeviceObject::firmwareVersionString() const
{
    return m_firmwareVersion.toString();
}

quint32 OathDeviceObject::serialNumber() const
{
    return m_serialNumber;
}

QString OathDeviceObject::id() const
{
    return m_id;
}

QString OathDeviceObject::deviceModelString() const
{
    return m_deviceModel;
}

quint32 OathDeviceObject::deviceModelCode() const
{
    return m_rawDeviceModel;
}

QString OathDeviceObject::formFactorString() const
{
    return m_formFactor;
}

QStringList OathDeviceObject::capabilitiesList() const
{
    return m_capabilities;
}

// Internal getters for raw values
Shared::YubiKeyModel OathDeviceObject::deviceModel() const
{
    return m_rawDeviceModel;
}

quint8 OathDeviceObject::formFactor() const
{
    return m_rawFormFactor;
}

qint64 OathDeviceObject::lastSeen() const
{
    const QDateTime lastSeenDateTime = m_service->getDeviceLastSeen(m_deviceId);
    if (lastSeenDateTime.isValid()) {
        return lastSeenDateTime.toMSecsSinceEpoch();
    }
    return 0; // Return 0 if device not in database or invalid timestamp
}

void OathDeviceObject::setName(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Cannot set empty name for device:"
                                    << m_deviceId;
        return;
    }

    if (m_name == name) {
        return;
    }

    // Update in service (database)
    if (m_service->setDeviceName(m_deviceId, name)) {
        m_name = name;
        Q_EMIT nameChanged(m_name);
        // Emit D-Bus PropertiesChanged signal (Device interface)
        emitDevicePropertyChanged(QStringLiteral("Name"), m_name);
        qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Name changed for device:" << m_deviceId
                                  << "to:" << m_name;
    } else {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Failed to set name for device:"
                                    << m_deviceId;
    }
}

void OathDeviceObject::setState(quint8 state, const QString &message)
{
    bool stateChanged = false;
    bool messageChanged = false;

    if (m_state != state) {
        m_state = state;
        stateChanged = true;
    }

    if (m_stateMessage != message) {
        m_stateMessage = message;
        messageChanged = true;
    }

    // Emit signals if values changed
    if (stateChanged) {
        Q_EMIT this->stateChanged(state);  // Qt property signal
        emitSessionPropertyChanged(QStringLiteral("State"), state);
        qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: State changed for device:" << m_deviceId
                                  << "to:" << static_cast<int>(state);
    }

    if (messageChanged) {
        Q_EMIT stateMessageChanged(message);
        emitSessionPropertyChanged(QStringLiteral("StateMessage"), message);
        if (!message.isEmpty()) {
            qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: State message for device:" << m_deviceId
                                      << "is:" << message;
        }
    }
}

bool OathDeviceObject::SavePassword(const QString &password)
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: SavePassword for device:" << m_deviceId;

    const bool success = m_service->savePassword(m_deviceId, password);

    if (success) {
        // Update cached properties
        if (!m_hasValidPassword) {
            m_hasValidPassword = true;
            Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
            // Emit D-Bus PropertiesChanged signal (DeviceSession interface)
            emitSessionPropertyChanged(QStringLiteral("HasValidPassword"), m_hasValidPassword);
        }
        qCInfo(OathDaemonLog) << "YubiKeyDeviceObject: Password saved for device:" << m_deviceId;
    } else {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Failed to save password for device:"
                                    << m_deviceId;
    }

    return success;
}

bool OathDeviceObject::ChangePassword(const QString &oldPassword, const QString &newPassword)
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: ChangePassword for device:" << m_deviceId;

    const bool success = m_service->changePassword(m_deviceId, oldPassword, newPassword);

    if (success) {
        // Update properties based on whether password was set or removed
        const bool requiresPassword = !newPassword.isEmpty();
        const bool hasValidPassword = !newPassword.isEmpty();

        if (m_requiresPassword != requiresPassword) {
            m_requiresPassword = requiresPassword;
            Q_EMIT requiresPasswordChanged(m_requiresPassword);
            emitDevicePropertyChanged(QStringLiteral("RequiresPassword"), m_requiresPassword);
        }

        if (m_hasValidPassword != hasValidPassword) {
            m_hasValidPassword = hasValidPassword;
            Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
            emitSessionPropertyChanged(QStringLiteral("HasValidPassword"), m_hasValidPassword);
        }

        if (newPassword.isEmpty()) {
            qCInfo(OathDaemonLog) << "YubiKeyDeviceObject: Password removed for device:" << m_deviceId;
        } else {
            qCInfo(OathDaemonLog) << "YubiKeyDeviceObject: Password changed for device:" << m_deviceId;
        }
    } else {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Failed to change password for device:"
                                    << m_deviceId;
    }

    return success;
}

void OathDeviceObject::Forget()
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Forget device:" << m_deviceId;

    m_service->forgetDevice(m_deviceId);

    // Note: The device object will be removed by Manager when deviceDisconnected signal is emitted
}

Shared::AddCredentialResult OathDeviceObject::AddCredential(const QString &name,
                                                               const QString &secret,
                                                               const QString &type,
                                                               const QString &algorithm,
                                                               int digits,
                                                               int period,
                                                               int counter,
                                                               bool requireTouch)
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: AddCredential for device:" << m_deviceId
                              << "name:" << name;

    auto result = m_service->addCredential(m_deviceId, name, secret, type, algorithm,
                                           digits, period, counter, requireTouch);

    // If success, result.message contains credential name - build path
    if (result.status == QLatin1String("Success")) {
        const QString credId = CredentialIdEncoder::encode(result.message);
        const QString path = QString::fromLatin1("%1/credentials/%2").arg(m_objectPath, credId);
        return {QLatin1String("Success"), path};
    }

    return result;
}

OathCredentialObject* OathDeviceObject::addCredential(const Shared::OathCredential &credential)
{
    return m_credentialManager->addCredential(credential);
}

void OathDeviceObject::removeCredential(const QString &credentialId)
{
    m_credentialManager->removeCredential(credentialId);
}

OathCredentialObject* OathDeviceObject::getCredential(const QString &credentialId) const
{
    return m_credentialManager->getCredential(credentialId);
}

QStringList OathDeviceObject::credentialPaths() const
{
    return m_credentialManager->credentialPaths();
}

void OathDeviceObject::updateCredentials()
{
    m_credentialManager->updateCredentials();
}

void OathDeviceObject::connectToDevice()
{
    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Connecting to device:" << m_deviceId;

    auto *device = m_service->getDevice(m_deviceId);
    if (!device) {
        qCWarning(OathDaemonLog) << "YubiKeyDeviceObject: Device not available:" << m_deviceId;
        // Set disconnected state
        setState(static_cast<quint8>(Shared::DeviceState::Disconnected), QString());
        return;
    }

    // Disconnect any previous connections to avoid duplicates
    // (Qt's connect with lambda creates new connection each time)
    disconnect(device, nullptr, this, nullptr);

    // Connect to device state signals
    connect(device, &OathDevice::stateChanged,
            this, [this](Shared::DeviceState newState) {
                auto *dev = m_service->getDevice(m_deviceId);
                const QString errorMsg = dev ? dev->lastError() : QString();
                setState(static_cast<quint8>(newState), errorMsg);
            });

    // Update current state from device
    setState(static_cast<quint8>(device->state()), device->lastError());

    qCDebug(OathDaemonLog) << "YubiKeyDeviceObject: Connected to device:" << m_deviceId
                              << "state:" << static_cast<int>(device->state());
}

QVariantMap OathDeviceObject::getManagedObjectData() const
{
    QVariantMap result;

    // pl.jkolo.yubikey.oath.Device interface properties (hardware + OATH application)
    QVariantMap deviceProps;
    deviceProps.insert(QLatin1String("Name"), m_name);
    deviceProps.insert(QLatin1String("RequiresPassword"), m_requiresPassword);
    deviceProps.insert(QLatin1String("FirmwareVersion"), m_firmwareVersion.toString());
    deviceProps.insert(QLatin1String("SerialNumber"), m_serialNumber);
    deviceProps.insert(QLatin1String("ID"), m_id);
    deviceProps.insert(QLatin1String("DeviceModel"), deviceModelString());
    deviceProps.insert(QLatin1String("DeviceModelCode"), m_rawDeviceModel);
    deviceProps.insert(QLatin1String("FormFactor"), formFactorString());
    deviceProps.insert(QLatin1String("Capabilities"), capabilitiesList());

    result.insert(QLatin1String(DEVICE_INTERFACE), deviceProps);

    // pl.jkolo.yubikey.oath.DeviceSession interface properties (connection state)
    QVariantMap sessionProps;
    sessionProps.insert(QLatin1String("State"), m_state);
    sessionProps.insert(QLatin1String("StateMessage"), m_stateMessage);
    sessionProps.insert(QLatin1String("HasValidPassword"), m_hasValidPassword);
    sessionProps.insert(QLatin1String("LastSeen"), lastSeen());

    result.insert(QLatin1String(DEVICE_SESSION_INTERFACE), sessionProps);

    return result;
}

QVariantMap OathDeviceObject::getManagedCredentialObjects() const
{
    return m_credentialManager->getManagedObjects();
}

void OathDeviceObject::emitPropertyChanged(const QString &interfaceName,
                                          const QString &propertyName,
                                          const QVariant &value)
{
    if (!m_registered) {
        return;
    }

    // Create PropertiesChanged signal
    // Signature: PropertiesChanged(interface_name, changed_properties, invalidated_properties)
    QDBusMessage signal = QDBusMessage::createSignal(
        m_objectPath,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged")
    );

    // Prepare arguments
    QVariantMap changedProperties;
    changedProperties.insert(propertyName, value);

    const QStringList invalidatedProperties; // Empty - we provide values directly

    signal << interfaceName
           << changedProperties
           << invalidatedProperties;

    // Send signal
    if (!m_connection.send(signal)) {
        qCWarning(OathDaemonLog) << "Failed to emit PropertiesChanged for"
                                     << propertyName << "on interface" << interfaceName
                                     << "on" << m_objectPath;
    } else {
        qCDebug(OathDaemonLog) << "Emitted PropertiesChanged:" << propertyName
                                   << "=" << value << "on interface" << interfaceName
                                   << "on" << m_objectPath;
    }
}

void OathDeviceObject::emitDevicePropertyChanged(const QString &propertyName, const QVariant &value)
{
    emitPropertyChanged(QStringLiteral("pl.jkolo.yubikey.oath.Device"), propertyName, value);
}

void OathDeviceObject::emitSessionPropertyChanged(const QString &propertyName, const QVariant &value)
{
    emitPropertyChanged(QStringLiteral("pl.jkolo.yubikey.oath.DeviceSession"), propertyName, value);
}

} // namespace Daemon
} // namespace YubiKeyOath
