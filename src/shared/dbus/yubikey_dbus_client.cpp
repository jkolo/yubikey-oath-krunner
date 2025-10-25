/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_client.h"
#include "dbus_connection_helper.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

YubiKeyDBusClient::YubiKeyDBusClient(QObject *parent)
    : QObject(parent)
    , m_interface(nullptr)
    , m_serviceWatcher(nullptr)
    , m_daemonAvailable(false)
{
    // Register custom types for D-Bus
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    qRegisterMetaType<CredentialInfo>("CredentialInfo");
    qRegisterMetaType<GenerateCodeResult>("GenerateCodeResult");
    qRegisterMetaType<QList<DeviceInfo>>("QList<DeviceInfo>");
    qRegisterMetaType<QList<CredentialInfo>>("QList<CredentialInfo>");

    qDBusRegisterMetaType<DeviceInfo>();
    qDBusRegisterMetaType<CredentialInfo>();
    qDBusRegisterMetaType<GenerateCodeResult>();
    qDBusRegisterMetaType<QList<DeviceInfo>>();
    qDBusRegisterMetaType<QList<CredentialInfo>>();

    // Create D-Bus interface
    m_interface = new QDBusInterface(
        QString::fromLatin1(SERVICE_NAME),
        QString::fromLatin1(OBJECT_PATH),
        QString::fromLatin1(INTERFACE_NAME),
        QDBusConnection::sessionBus(),
        this
    );

    // Check initial availability
    checkDaemonAvailability();

    // Setup service watcher to detect daemon start/stop
    m_serviceWatcher = new QDBusServiceWatcher(
        QString::fromLatin1(SERVICE_NAME),
        QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this
    );

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &YubiKeyDBusClient::onDBusServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &YubiKeyDBusClient::onDBusServiceUnregistered);

    // Setup signal connections if daemon is available
    if (m_daemonAvailable) {
        setupSignalConnections();
    }
}

// QDBusInterface and QDBusServiceWatcher will be deleted by Qt parent-child relationship
YubiKeyDBusClient::~YubiKeyDBusClient() = default;

QList<DeviceInfo> YubiKeyDBusClient::listDevices()
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return {};
    }

    QDBusReply<QList<DeviceInfo>> reply = m_interface->call(QStringLiteral("ListDevices"));
    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: ListDevices failed:" << reply.error().message();
        return {};
    }

    return reply.value();
}

QList<CredentialInfo> YubiKeyDBusClient::getCredentials(const QString &deviceId)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return {};
    }

    QDBusReply<QList<CredentialInfo>> reply = m_interface->call(
        QStringLiteral("GetCredentials"),
        deviceId
    );

    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: GetCredentials failed:" << reply.error().message();
        return {};
    }

    return reply.value();
}

GenerateCodeResult YubiKeyDBusClient::generateCode(const QString &deviceId,
                                                    const QString &credentialName)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return {.code = QString(), .validUntil = 0};
    }

    // GenerateCode returns GenerateCodeResult struct (sx)
    QDBusReply<GenerateCodeResult> reply = m_interface->call(
        QStringLiteral("GenerateCode"),
        deviceId,
        credentialName
    );

    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: GenerateCode failed:" << reply.error().message();
        return {.code = QString(), .validUntil = 0};
    }

    return reply.value();
}

bool YubiKeyDBusClient::savePassword(const QString &deviceId, const QString &password)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(
        QStringLiteral("SavePassword"),
        deviceId,
        password
    );

    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: SavePassword failed:" << reply.error().message();
        return false;
    }

    return reply.value();
}

void YubiKeyDBusClient::forgetDevice(const QString &deviceId)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return;
    }

    m_interface->call(QStringLiteral("ForgetDevice"), deviceId);
}

bool YubiKeyDBusClient::setDeviceName(const QString &deviceId, const QString &newName)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(
        QStringLiteral("SetDeviceName"),
        deviceId,
        newName
    );

    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: SetDeviceName failed:" << reply.error().message();
        return false;
    }

    return reply.value();
}

QString YubiKeyDBusClient::getDeviceName(const QString &deviceId)
{
    QList<DeviceInfo> devices = listDevices();
    for (const auto &device : devices) {
        if (device.deviceId == deviceId) {
            return device.deviceName;
        }
    }
    return deviceId; // Fallback to device ID if not found
}

QString YubiKeyDBusClient::addCredential(const QString &deviceId,
                                         const QString &name,
                                         const QString &secret,
                                         const QString &type,
                                         const QString &algorithm,
                                         int digits,
                                         int period,
                                         int counter,
                                         bool requireTouch)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return i18n("Daemon not available");
    }

    QDBusReply<QString> reply = m_interface->call(
        QStringLiteral("AddCredential"),
        deviceId,
        name,
        secret,
        type,
        algorithm,
        digits,
        period,
        counter,
        requireTouch
    );

    if (!reply.isValid()) {
        qWarning() << "YubiKeyDBusClient: AddCredential failed:" << reply.error().message();
        return reply.error().message();
    }

    return reply.value(); // Empty string = success, otherwise error message
}

bool YubiKeyDBusClient::copyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return false;
    }

    // Call asynchronously (fire-and-forget) to avoid blocking KRunner window
    // Daemon will handle all UI (touch notifications, copying, code notification, etc.)
    m_interface->asyncCall(
        QStringLiteral("CopyCodeToClipboard"),
        deviceId,
        credentialName
    );

    // Return true immediately - daemon will handle the workflow
    return true;
}

bool YubiKeyDBusClient::typeCode(const QString &deviceId, const QString &credentialName)
{
    if (!m_daemonAvailable) {
        qWarning() << "YubiKeyDBusClient: Daemon not available";
        return false;
    }

    // Call asynchronously (fire-and-forget) to avoid blocking KRunner window
    // Daemon will handle all UI (touch notifications, typing, etc.)
    m_interface->asyncCall(
        QStringLiteral("TypeCode"),
        deviceId,
        credentialName
    );

    // Return true immediately - daemon will handle the workflow
    return true;
}

bool YubiKeyDBusClient::isDaemonAvailable() const
{
    return m_daemonAvailable;
}

void YubiKeyDBusClient::onDBusServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qDebug() << "YubiKeyDBusClient: Daemon registered";

    m_daemonAvailable = true;
    setupSignalConnections();
}

void YubiKeyDBusClient::onDBusServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qWarning() << "YubiKeyDBusClient: Daemon unregistered";

    m_daemonAvailable = false;
    Q_EMIT daemonUnavailable();
}

void YubiKeyDBusClient::setupSignalConnections()
{
    if (!m_interface || !m_interface->isValid()) {
        return;
    }

    // Connect D-Bus signals to our signals using helper
    int connected = DBusConnectionHelper::connectSignals(
        QString::fromLatin1(SERVICE_NAME),
        QString::fromLatin1(OBJECT_PATH),
        QString::fromLatin1(INTERFACE_NAME),
        this,
        {
            {"DeviceConnected", SIGNAL(deviceConnected(QString))},
            {"DeviceDisconnected", SIGNAL(deviceDisconnected(QString))},
            {"CredentialsUpdated", SIGNAL(credentialsUpdated(QString))},
            {"DeviceForgetRequested", SIGNAL(deviceForgetRequested(QString))}
        }
    );

    qDebug() << "YubiKeyDBusClient: Signal connections established:" << connected << "of 4";
}

void YubiKeyDBusClient::checkDaemonAvailability()
{
    m_daemonAvailable = m_interface && m_interface->isValid();

    if (m_daemonAvailable) {
        qDebug() << "YubiKeyDBusClient: Daemon is available";
    } else {
        qWarning() << "YubiKeyDBusClient: Daemon not available, will auto-start on first use";
    }
}

} // namespace YubiKey
} // namespace KRunner
