/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device_proxy.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QLatin1String>
#include <QLoggingCategory>
#include <KLocalizedString>

Q_LOGGING_CATEGORY(OathDeviceProxyLog, "pl.jkolo.yubikey.oath.daemon.device.proxy")

namespace YubiKeyOath {
namespace Shared {

OathDeviceProxy::OathDeviceProxy(const QString &objectPath,
                                       const QVariantMap &deviceProperties,
                                       const QHash<QString, QVariantMap> &credentialObjects,
                                       QObject *parent)
    : QObject(parent)
    , m_objectPath(objectPath)
    , m_interface(nullptr)
    , m_requiresPassword(false)
    , m_hasValidPassword(false)
{
    // Create D-Bus interface for method calls
    m_interface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                     objectPath,
                                     QLatin1String(INTERFACE_NAME),
                                     QDBusConnection::sessionBus(),
                                     this);

    if (!m_interface->isValid()) {
        qCWarning(OathDeviceProxyLog) << "Failed to create D-Bus interface for device at"
                                          << objectPath
                                          << "Error:" << m_interface->lastError().message();
    }

    // Extract and cache device properties
    m_name = deviceProperties.value(QStringLiteral("Name")).toString();
    m_requiresPassword = deviceProperties.value(QStringLiteral("RequiresPassword")).toBool();
    m_hasValidPassword = deviceProperties.value(QStringLiteral("HasValidPassword")).toBool();

    // Extract firmware version (FirmwareVersion property is QString)
    QString const firmwareVersionStr = deviceProperties.value(QStringLiteral("FirmwareVersion")).toString();
    if (!firmwareVersionStr.isEmpty()) {
        m_firmwareVersion = Version::fromString(firmwareVersionStr);
    }

    // Extract device ID (const property) - hex device identifier
    m_deviceId = deviceProperties.value(QStringLiteral("ID")).toString();

    // Extract serial number and device model (const properties)
    m_serialNumber = deviceProperties.value(QStringLiteral("SerialNumber")).toUInt();
    m_deviceModel = deviceProperties.value(QStringLiteral("DeviceModel")).toString();
    m_deviceModelCode = deviceProperties.value(QStringLiteral("DeviceModelCode")).toUInt();
    m_formFactor = deviceProperties.value(QStringLiteral("FormFactor")).toString();
    m_capabilities = deviceProperties.value(QStringLiteral("Capabilities")).toStringList();

    // Extract last seen timestamp
    const qint64 lastSeenMsecs = deviceProperties.value(QStringLiteral("LastSeen")).toLongLong();
    m_lastSeen = QDateTime::fromMSecsSinceEpoch(lastSeenMsecs);

    // Extract device state properties
    const auto stateValue = deviceProperties.value(QStringLiteral("State")).value<quint8>();
    m_state = static_cast<DeviceState>(stateValue);
    m_stateMessage = deviceProperties.value(QStringLiteral("StateMessage")).toString();

    qCDebug(OathDeviceProxyLog) << "Created device proxy for" << m_name
                                    << "SerialNumber:" << m_serialNumber
                                    << "at" << objectPath;

    // Create credential proxies for all initial credentials
    for (auto it = credentialObjects.constBegin(); it != credentialObjects.constEnd(); ++it) {
        addCredentialProxy(it.key(), it.value());
    }

    // Connect to D-Bus signals
    connectToSignals();
}

OathDeviceProxy::~OathDeviceProxy()
{
    qCDebug(OathDeviceProxyLog) << "Destroying device proxy for" << m_name;
}

void OathDeviceProxy::connectToSignals()
{
    if (!m_interface || !m_interface->isValid()) {
        return;
    }

    // Connect to CredentialAdded/CredentialRemoved signals
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("CredentialAdded"),
        this,
        SLOT(onCredentialAddedSignal(QDBusObjectPath))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("CredentialRemoved"),
        this,
        SLOT(onCredentialRemovedSignal(QDBusObjectPath))
    );

    // Connect to PropertiesChanged for property updates
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(PROPERTIES_INTERFACE),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList))
    );
}

QList<OathCredentialProxy*> OathDeviceProxy::credentials() const
{
    return m_credentials.values();
}

OathCredentialProxy* OathDeviceProxy::getCredential(const QString &credentialName) const
{
    return m_credentials.value(credentialName, nullptr);
}

bool OathDeviceProxy::savePassword(const QString &password)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathDeviceProxyLog) << "Cannot save password: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("SavePassword"), password);

    if (!reply.isValid()) {
        qCWarning(OathDeviceProxyLog) << "SavePassword failed for" << m_name
                                          << "Error:" << reply.error().message();
        return false;
    }

    bool const success = reply.value();
    qCDebug(OathDeviceProxyLog) << "SavePassword for" << m_name << "Result:" << success;
    return success;
}

bool OathDeviceProxy::changePassword(const QString &oldPassword, const QString &newPassword)
{
    QString errorMessage;  // Unused in this overload
    return changePassword(oldPassword, newPassword, errorMessage);
}

bool OathDeviceProxy::changePassword(const QString &oldPassword, const QString &newPassword, QString &errorMessage)
{
    if (!m_interface || !m_interface->isValid()) {
        errorMessage = QStringLiteral("D-Bus interface invalid");
        qCWarning(OathDeviceProxyLog) << "Cannot change password:" << errorMessage;
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("ChangePassword"),
                                                oldPassword, newPassword);

    if (!reply.isValid()) {
        errorMessage = reply.error().message();
        qCWarning(OathDeviceProxyLog) << "ChangePassword failed for" << m_name
                                          << "Error:" << errorMessage;
        return false;
    }

    bool const success = reply.value();
    qCDebug(OathDeviceProxyLog) << "ChangePassword for" << m_name << "Result:" << success;

    if (!success) {
        // D-Bus call succeeded but operation failed
        // Unfortunately, standard D-Bus doesn't provide error message when call succeeds but returns false
        // We can only provide a generic message here
        errorMessage = i18n("Password change failed. The current password may be incorrect, or the YubiKey may not be accessible.");
    }

    return success;
}

void OathDeviceProxy::forget()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathDeviceProxyLog) << "Cannot forget device: D-Bus interface invalid";
        return;
    }

    QDBusReply<void> reply = m_interface->call(QStringLiteral("Forget"));

    if (!reply.isValid()) {
        qCWarning(OathDeviceProxyLog) << "Forget failed for" << m_name
                                          << "Error:" << reply.error().message();
        return;
    }

    qCDebug(OathDeviceProxyLog) << "Forgot device" << m_name;
}

AddCredentialResult OathDeviceProxy::addCredential(const QString &name,
                                                      const QString &secret,
                                                      const QString &type,
                                                      const QString &algorithm,
                                                      int digits,
                                                      int period,
                                                      int counter,
                                                      bool requireTouch)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathDeviceProxyLog) << "Cannot add credential: D-Bus interface invalid";
        return AddCredentialResult{QStringLiteral("Error"), QStringLiteral("D-Bus interface invalid")};
    }

    QDBusReply<AddCredentialResult> reply = m_interface->call(
        QStringLiteral("AddCredential"),
        name, secret, type, algorithm, digits, period, counter, requireTouch
    );

    if (!reply.isValid()) {
        qCWarning(OathDeviceProxyLog) << "AddCredential failed for" << m_name
                                          << "Error:" << reply.error().message();
        return AddCredentialResult{QStringLiteral("Error"), reply.error().message()};
    }

    auto result = reply.value();
    qCDebug(OathDeviceProxyLog) << "AddCredential for" << m_name
                                    << "Status:" << result.status
                                    << "PathOrMessage:" << result.message;
    return result;
}

bool OathDeviceProxy::setName(const QString &newName)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathDeviceProxyLog) << "Cannot set name: D-Bus interface invalid";
        return false;
    }

    // Use D-Bus Properties interface to set Name property
    QDBusInterface propsInterface(QLatin1String(SERVICE_NAME),
                                   m_objectPath,
                                   QLatin1String(PROPERTIES_INTERFACE),
                                   QDBusConnection::sessionBus());

    QDBusReply<void> reply = propsInterface.call(QStringLiteral("Set"),
                                                  QLatin1String(INTERFACE_NAME),
                                                  QStringLiteral("Name"),
                                                  QVariant::fromValue(QDBusVariant(newName)));

    if (!reply.isValid()) {
        qCWarning(OathDeviceProxyLog) << "setName failed for" << m_name
                                          << "Error:" << reply.error().message();
        return false;
    }

    // Update cached name (PropertiesChanged signal will also update it)
    m_name = newName;
    Q_EMIT nameChanged(newName);

    qCDebug(OathDeviceProxyLog) << "Updated device name to" << newName;
    return true;
}

DeviceInfo OathDeviceProxy::toDeviceInfo() const
{
    DeviceInfo info;
    info._internalDeviceId = m_deviceId;
    info.deviceName = m_name;
    info.firmwareVersion = m_firmwareVersion;
    info.serialNumber = m_serialNumber;
    info.deviceModel = m_deviceModel;
    info.deviceModelCode = m_deviceModelCode;
    info.capabilities = m_capabilities;
    info.formFactor = m_formFactor;
    info.state = m_state;
    info.requiresPassword = m_requiresPassword;
    info.hasValidPassword = m_hasValidPassword;
    info.lastSeen = m_lastSeen;
    return info;
}

void OathDeviceProxy::onCredentialAddedSignal(const QDBusObjectPath &credentialPath)
{
    QString const path = credentialPath.path();
    qCDebug(OathDeviceProxyLog) << "CredentialAdded signal received for" << path;

    // Fetch credential properties via D-Bus Properties interface
    QDBusInterface propsInterface(QLatin1String(SERVICE_NAME),
                                   path,
                                   QLatin1String(PROPERTIES_INTERFACE),
                                   QDBusConnection::sessionBus());

    QDBusReply<QVariantMap> reply = propsInterface.call(
        QStringLiteral("GetAll"),
        QStringLiteral("pl.jkolo.yubikey.oath.Credential")
    );

    if (!reply.isValid()) {
        qCWarning(OathDeviceProxyLog) << "Failed to get credential properties for" << path
                                          << "Error:" << reply.error().message();
        return;
    }

    addCredentialProxy(path, reply.value());
}

void OathDeviceProxy::onCredentialRemovedSignal(const QDBusObjectPath &credentialPath)
{
    QString const path = credentialPath.path();
    qCDebug(OathDeviceProxyLog) << "CredentialRemoved signal received for" << path;
    removeCredentialProxy(path);
}

void OathDeviceProxy::onPropertiesChanged(const QString &interfaceName,
                                            const QVariantMap &changedProperties,
                                            const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)

    if (interfaceName != QLatin1String(INTERFACE_NAME)) {
        return;
    }

    qCDebug(OathDeviceProxyLog) << "PropertiesChanged for" << m_name
                                    << "Changed properties:" << changedProperties.keys();

    // Update cached properties
    if (changedProperties.contains(QStringLiteral("Name"))) {
        m_name = changedProperties.value(QStringLiteral("Name")).toString();
        Q_EMIT nameChanged(m_name);
    }

    if (changedProperties.contains(QStringLiteral("RequiresPassword"))) {
        m_requiresPassword = changedProperties.value(QStringLiteral("RequiresPassword")).toBool();
        Q_EMIT requiresPasswordChanged(m_requiresPassword);
    }

    if (changedProperties.contains(QStringLiteral("HasValidPassword"))) {
        m_hasValidPassword = changedProperties.value(QStringLiteral("HasValidPassword")).toBool();
        Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
    }

    if (changedProperties.contains(QStringLiteral("State"))) {
        const auto stateValue = changedProperties.value(QStringLiteral("State")).value<quint8>();
        m_state = static_cast<DeviceState>(stateValue);
        Q_EMIT stateChanged(m_state);
    }

    if (changedProperties.contains(QStringLiteral("StateMessage"))) {
        m_stateMessage = changedProperties.value(QStringLiteral("StateMessage")).toString();
        Q_EMIT stateMessageChanged(m_stateMessage);
    }
}

void OathDeviceProxy::addCredentialProxy(const QString &objectPath,
                                           const QVariantMap &properties)
{
    // Extract credential name from properties
    QString const credentialName = properties.value(QStringLiteral("FullName")).toString();

    if (credentialName.isEmpty()) {
        qCWarning(OathDeviceProxyLog) << "Cannot add credential proxy: name is empty";
        return;
    }

    // Check if already exists
    if (m_credentials.contains(credentialName)) {
        qCDebug(OathDeviceProxyLog) << "Credential" << credentialName << "already exists, skipping";
        return;
    }

    // Create credential proxy (this object becomes parent, so proxy is auto-deleted)
    auto *credential = new OathCredentialProxy(objectPath, properties, this);
    m_credentials.insert(credentialName, credential);

    qCDebug(OathDeviceProxyLog) << "Added credential proxy:" << credentialName;
    Q_EMIT credentialAdded(credential);
}

void OathDeviceProxy::removeCredentialProxy(const QString &objectPath)
{
    // Find credential by object path
    QString credentialName;
    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        if (it.value()->objectPath() == objectPath) {
            credentialName = it.key();
            break;
        }
    }

    if (credentialName.isEmpty()) {
        qCDebug(OathDeviceProxyLog) << "Credential not found for path" << objectPath;
        return;
    }

    // Remove and delete credential proxy
    auto *credential = m_credentials.take(credentialName);
    if (credential) {
        qCDebug(OathDeviceProxyLog) << "Removed credential proxy:" << credentialName;
        Q_EMIT credentialRemoved(credentialName);
        credential->deleteLater();
    }
}

} // namespace Shared
} // namespace YubiKeyOath
