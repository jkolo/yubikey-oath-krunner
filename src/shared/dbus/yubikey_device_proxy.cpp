/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_device_proxy.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QLatin1String>
#include <QLoggingCategory>
#include <KLocalizedString>

Q_LOGGING_CATEGORY(YubiKeyDeviceProxyLog, "pl.jkolo.yubikey.oath.daemon.device.proxy")

namespace YubiKeyOath {
namespace Shared {

YubiKeyDeviceProxy::YubiKeyDeviceProxy(const QString &objectPath,
                                       const QVariantMap &deviceProperties,
                                       const QHash<QString, QVariantMap> &credentialObjects,
                                       QObject *parent)
    : QObject(parent)
    , m_objectPath(objectPath)
    , m_interface(nullptr)
    , m_isConnected(false)
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
        qCWarning(YubiKeyDeviceProxyLog) << "Failed to create D-Bus interface for device at"
                                          << objectPath
                                          << "Error:" << m_interface->lastError().message();
    }

    // Extract and cache device properties
    m_deviceId = deviceProperties.value(QStringLiteral("DeviceId")).toString();
    m_name = deviceProperties.value(QStringLiteral("Name")).toString();
    m_isConnected = deviceProperties.value(QStringLiteral("IsConnected")).toBool();
    m_requiresPassword = deviceProperties.value(QStringLiteral("RequiresPassword")).toBool();
    m_hasValidPassword = deviceProperties.value(QStringLiteral("HasValidPassword")).toBool();

    qCDebug(YubiKeyDeviceProxyLog) << "Created device proxy for" << m_name
                                    << "DeviceId:" << m_deviceId
                                    << "at" << objectPath;

    // Create credential proxies for all initial credentials
    for (auto it = credentialObjects.constBegin(); it != credentialObjects.constEnd(); ++it) {
        addCredentialProxy(it.key(), it.value());
    }

    // Connect to D-Bus signals
    connectToSignals();
}

YubiKeyDeviceProxy::~YubiKeyDeviceProxy()
{
    qCDebug(YubiKeyDeviceProxyLog) << "Destroying device proxy for" << m_name;
}

void YubiKeyDeviceProxy::connectToSignals()
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

QList<YubiKeyCredentialProxy*> YubiKeyDeviceProxy::credentials() const
{
    return m_credentials.values();
}

YubiKeyCredentialProxy* YubiKeyDeviceProxy::getCredential(const QString &credentialName) const
{
    return m_credentials.value(credentialName, nullptr);
}

bool YubiKeyDeviceProxy::savePassword(const QString &password)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot save password: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("SavePassword"), password);

    if (!reply.isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "SavePassword failed for" << m_name
                                          << "Error:" << reply.error().message();
        return false;
    }

    bool const success = reply.value();
    qCDebug(YubiKeyDeviceProxyLog) << "SavePassword for" << m_name << "Result:" << success;
    return success;
}

bool YubiKeyDeviceProxy::changePassword(const QString &oldPassword, const QString &newPassword)
{
    QString errorMessage;  // Unused in this overload
    return changePassword(oldPassword, newPassword, errorMessage);
}

bool YubiKeyDeviceProxy::changePassword(const QString &oldPassword, const QString &newPassword, QString &errorMessage)
{
    if (!m_interface || !m_interface->isValid()) {
        errorMessage = QStringLiteral("D-Bus interface invalid");
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot change password:" << errorMessage;
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("ChangePassword"),
                                                oldPassword, newPassword);

    if (!reply.isValid()) {
        errorMessage = reply.error().message();
        qCWarning(YubiKeyDeviceProxyLog) << "ChangePassword failed for" << m_name
                                          << "Error:" << errorMessage;
        return false;
    }

    bool const success = reply.value();
    qCDebug(YubiKeyDeviceProxyLog) << "ChangePassword for" << m_name << "Result:" << success;

    if (!success) {
        // D-Bus call succeeded but operation failed
        // Unfortunately, standard D-Bus doesn't provide error message when call succeeds but returns false
        // We can only provide a generic message here
        errorMessage = i18n("Password change failed. The current password may be incorrect, or the YubiKey may not be accessible.");
    }

    return success;
}

void YubiKeyDeviceProxy::forget()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot forget device: D-Bus interface invalid";
        return;
    }

    QDBusReply<void> reply = m_interface->call(QStringLiteral("Forget"));

    if (!reply.isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Forget failed for" << m_name
                                          << "Error:" << reply.error().message();
        return;
    }

    qCDebug(YubiKeyDeviceProxyLog) << "Forgot device" << m_name;
}

AddCredentialResult YubiKeyDeviceProxy::addCredential(const QString &name,
                                                      const QString &secret,
                                                      const QString &type,
                                                      const QString &algorithm,
                                                      int digits,
                                                      int period,
                                                      int counter,
                                                      bool requireTouch)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot add credential: D-Bus interface invalid";
        return AddCredentialResult{QStringLiteral("Error"), QStringLiteral("D-Bus interface invalid")};
    }

    QDBusReply<AddCredentialResult> reply = m_interface->call(
        QStringLiteral("AddCredential"),
        name, secret, type, algorithm, digits, period, counter, requireTouch
    );

    if (!reply.isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "AddCredential failed for" << m_name
                                          << "Error:" << reply.error().message();
        return AddCredentialResult{QStringLiteral("Error"), reply.error().message()};
    }

    auto result = reply.value();
    qCDebug(YubiKeyDeviceProxyLog) << "AddCredential for" << m_name
                                    << "Status:" << result.status
                                    << "PathOrMessage:" << result.message;
    return result;
}

bool YubiKeyDeviceProxy::setName(const QString &newName)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot set name: D-Bus interface invalid";
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
        qCWarning(YubiKeyDeviceProxyLog) << "setName failed for" << m_name
                                          << "Error:" << reply.error().message();
        return false;
    }

    // Update cached name (PropertiesChanged signal will also update it)
    m_name = newName;
    Q_EMIT nameChanged(newName);

    qCDebug(YubiKeyDeviceProxyLog) << "Updated device name to" << newName;
    return true;
}

DeviceInfo YubiKeyDeviceProxy::toDeviceInfo() const
{
    DeviceInfo info;
    info.deviceId = m_deviceId;
    info.deviceName = m_name;
    info.isConnected = m_isConnected;
    info.requiresPassword = m_requiresPassword;
    info.hasValidPassword = m_hasValidPassword;
    return info;
}

void YubiKeyDeviceProxy::onCredentialAddedSignal(const QDBusObjectPath &credentialPath)
{
    QString const path = credentialPath.path();
    qCDebug(YubiKeyDeviceProxyLog) << "CredentialAdded signal received for" << path;

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
        qCWarning(YubiKeyDeviceProxyLog) << "Failed to get credential properties for" << path
                                          << "Error:" << reply.error().message();
        return;
    }

    addCredentialProxy(path, reply.value());
}

void YubiKeyDeviceProxy::onCredentialRemovedSignal(const QDBusObjectPath &credentialPath)
{
    QString const path = credentialPath.path();
    qCDebug(YubiKeyDeviceProxyLog) << "CredentialRemoved signal received for" << path;
    removeCredentialProxy(path);
}

void YubiKeyDeviceProxy::onPropertiesChanged(const QString &interfaceName,
                                            const QVariantMap &changedProperties,
                                            const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)

    if (interfaceName != QLatin1String(INTERFACE_NAME)) {
        return;
    }

    qCDebug(YubiKeyDeviceProxyLog) << "PropertiesChanged for" << m_name
                                    << "Changed properties:" << changedProperties.keys();

    // Update cached properties
    if (changedProperties.contains(QStringLiteral("Name"))) {
        m_name = changedProperties.value(QStringLiteral("Name")).toString();
        Q_EMIT nameChanged(m_name);
    }

    if (changedProperties.contains(QStringLiteral("IsConnected"))) {
        m_isConnected = changedProperties.value(QStringLiteral("IsConnected")).toBool();
        Q_EMIT connectionChanged(m_isConnected);
    }

    if (changedProperties.contains(QStringLiteral("RequiresPassword"))) {
        m_requiresPassword = changedProperties.value(QStringLiteral("RequiresPassword")).toBool();
    }

    if (changedProperties.contains(QStringLiteral("HasValidPassword"))) {
        m_hasValidPassword = changedProperties.value(QStringLiteral("HasValidPassword")).toBool();
    }
}

void YubiKeyDeviceProxy::addCredentialProxy(const QString &objectPath,
                                           const QVariantMap &properties)
{
    // Extract credential name from properties
    QString const credentialName = properties.value(QStringLiteral("Name")).toString();

    if (credentialName.isEmpty()) {
        qCWarning(YubiKeyDeviceProxyLog) << "Cannot add credential proxy: name is empty";
        return;
    }

    // Check if already exists
    if (m_credentials.contains(credentialName)) {
        qCDebug(YubiKeyDeviceProxyLog) << "Credential" << credentialName << "already exists, skipping";
        return;
    }

    // Create credential proxy (this object becomes parent, so proxy is auto-deleted)
    auto *credential = new YubiKeyCredentialProxy(objectPath, properties, this);
    m_credentials.insert(credentialName, credential);

    qCDebug(YubiKeyDeviceProxyLog) << "Added credential proxy:" << credentialName;
    Q_EMIT credentialAdded(credential);
}

void YubiKeyDeviceProxy::removeCredentialProxy(const QString &objectPath)
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
        qCDebug(YubiKeyDeviceProxyLog) << "Credential not found for path" << objectPath;
        return;
    }

    // Remove and delete credential proxy
    auto *credential = m_credentials.take(credentialName);
    if (credential) {
        qCDebug(YubiKeyDeviceProxyLog) << "Removed credential proxy:" << credentialName;
        Q_EMIT credentialRemoved(credentialName);
        credential->deleteLater();
    }
}

} // namespace Shared
} // namespace YubiKeyOath
