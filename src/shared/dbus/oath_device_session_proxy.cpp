/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device_session_proxy.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QLatin1String>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(OathDeviceSessionProxyLog, "pl.jkolo.yubikey.oath.client.device.session.proxy")

namespace YubiKeyOath {
namespace Shared {

OathDeviceSessionProxy::OathDeviceSessionProxy(const QString &objectPath,
                                                 const QVariantMap &sessionProperties,
                                                 QObject *parent)
    : QObject(parent)
    , m_objectPath(objectPath)
    , m_interface(nullptr)
{
    // Create D-Bus interface for method calls
    m_interface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                     objectPath,
                                     QLatin1String(INTERFACE_NAME),
                                     QDBusConnection::sessionBus(),
                                     this);

    if (!m_interface->isValid()) {
        qCWarning(OathDeviceSessionProxyLog) << "Failed to create D-Bus interface for device session at"
                                               << objectPath
                                               << "Error:" << m_interface->lastError().message();
    }

    // Extract and cache session properties
    const auto stateValue = sessionProperties.value(QStringLiteral("State")).value<quint8>();
    m_state = static_cast<DeviceState>(stateValue);
    m_stateMessage = sessionProperties.value(QStringLiteral("StateMessage")).toString();
    m_hasValidPassword = sessionProperties.value(QStringLiteral("HasValidPassword")).toBool();

    // Extract last seen timestamp
    const qint64 lastSeenMsecs = sessionProperties.value(QStringLiteral("LastSeen")).toLongLong();
    m_lastSeen = QDateTime::fromMSecsSinceEpoch(lastSeenMsecs);

    qCDebug(OathDeviceSessionProxyLog) << "Created device session proxy for"
                                        << objectPath
                                        << "State:" << deviceStateToString(m_state);

    // Connect to D-Bus signals
    connectToSignals();
}

OathDeviceSessionProxy::~OathDeviceSessionProxy()
{
    qCDebug(OathDeviceSessionProxyLog) << "Destroying device session proxy for" << m_objectPath;
}

void OathDeviceSessionProxy::connectToSignals()
{
    if (!m_interface || !m_interface->isValid()) {
        return;
    }

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

bool OathDeviceSessionProxy::savePassword(const QString &password)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathDeviceSessionProxyLog) << "Cannot save password: D-Bus interface invalid";
        return false;
    }

    const QDBusReply<bool> reply = m_interface->call(QStringLiteral("SavePassword"), password);

    if (!reply.isValid()) {
        qCWarning(OathDeviceSessionProxyLog) << "SavePassword failed for" << m_objectPath
                                              << "Error:" << reply.error().message();
        return false;
    }

    bool const success = reply.value();
    qCDebug(OathDeviceSessionProxyLog) << "SavePassword for" << m_objectPath << "Result:" << success;
    return success;
}

void OathDeviceSessionProxy::onPropertiesChanged(const QString &interfaceName,
                                                   const QVariantMap &changedProperties,
                                                   const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)

    if (interfaceName != QLatin1String(INTERFACE_NAME)) {
        return;
    }

    qCDebug(OathDeviceSessionProxyLog) << "PropertiesChanged for" << m_objectPath
                                        << "Changed properties:" << changedProperties.keys();

    // Update cached properties
    if (changedProperties.contains(QStringLiteral("State"))) {
        const auto stateValue = changedProperties.value(QStringLiteral("State")).value<quint8>();
        m_state = static_cast<DeviceState>(stateValue);
        Q_EMIT stateChanged(m_state);
    }

    if (changedProperties.contains(QStringLiteral("StateMessage"))) {
        m_stateMessage = changedProperties.value(QStringLiteral("StateMessage")).toString();
        Q_EMIT stateMessageChanged(m_stateMessage);
    }

    if (changedProperties.contains(QStringLiteral("HasValidPassword"))) {
        m_hasValidPassword = changedProperties.value(QStringLiteral("HasValidPassword")).toBool();
        Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
    }

    if (changedProperties.contains(QStringLiteral("LastSeen"))) {
        const qint64 lastSeenMsecs = changedProperties.value(QStringLiteral("LastSeen")).toLongLong();
        m_lastSeen = QDateTime::fromMSecsSinceEpoch(lastSeenMsecs);
        Q_EMIT lastSeenChanged(m_lastSeen);
    }
}

} // namespace Shared
} // namespace YubiKeyOath
