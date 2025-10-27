/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_credential_proxy.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QLatin1String>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(YubiKeyCredentialProxyLog, "pl.jkolo.yubikey.oath.daemon.credential.proxy")

namespace YubiKeyOath {
namespace Shared {

// Register D-Bus types on first use
static void registerDBusTypes()
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<GenerateCodeResult>();
        registered = true;
    }
}

YubiKeyCredentialProxy::YubiKeyCredentialProxy(const QString &objectPath,
                                               const QVariantMap &properties,
                                               QObject *parent)
    : QObject(parent)
    , m_objectPath(objectPath)
    , m_interface(nullptr)
    , m_requiresTouch(false)
    , m_digits(6)
    , m_period(30)
{
    // Register D-Bus types
    registerDBusTypes();
    // Create D-Bus interface for method calls
    m_interface = new QDBusInterface(QLatin1String(SERVICE_NAME),
                                     objectPath,
                                     QLatin1String(INTERFACE_NAME),
                                     QDBusConnection::sessionBus(),
                                     this);

    if (!m_interface->isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Failed to create D-Bus interface for credential at"
                                              << objectPath
                                              << "Error:" << m_interface->lastError().message();
    }

    // Extract and cache properties from GetManagedObjects() result
    m_name = properties.value(QStringLiteral("Name")).toString();
    m_issuer = properties.value(QStringLiteral("Issuer")).toString();
    m_username = properties.value(QStringLiteral("Username")).toString();
    m_requiresTouch = properties.value(QStringLiteral("RequiresTouch")).toBool();
    m_type = properties.value(QStringLiteral("Type")).toString();
    m_algorithm = properties.value(QStringLiteral("Algorithm")).toString();
    m_digits = properties.value(QStringLiteral("Digits")).toInt();
    m_period = properties.value(QStringLiteral("Period")).toInt();
    m_deviceId = properties.value(QStringLiteral("DeviceId")).toString();

    qCDebug(YubiKeyCredentialProxyLog) << "Created credential proxy for" << m_name
                                       << "at" << objectPath;
}

YubiKeyCredentialProxy::~YubiKeyCredentialProxy()
{
    qCDebug(YubiKeyCredentialProxyLog) << "Destroying credential proxy for" << m_name;
}

GenerateCodeResult YubiKeyCredentialProxy::generateCode()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Cannot generate code: D-Bus interface invalid";
        return GenerateCodeResult{QString(), 0};
    }

    QDBusReply<GenerateCodeResult> reply = m_interface->call(QStringLiteral("GenerateCode"));

    if (!reply.isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "GenerateCode failed for" << m_name
                                              << "Error:" << reply.error().message();
        return GenerateCodeResult{QString(), 0};
    }

    auto result = reply.value();
    qCDebug(YubiKeyCredentialProxyLog) << "Generated code for" << m_name
                                       << "Valid until:" << result.validUntil;
    return result;
}

bool YubiKeyCredentialProxy::copyToClipboard()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Cannot copy to clipboard: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("CopyToClipboard"));

    if (!reply.isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "CopyToClipboard failed for" << m_name
                                              << "Error:" << reply.error().message();
        return false;
    }

    bool success = reply.value();
    qCDebug(YubiKeyCredentialProxyLog) << "CopyToClipboard for" << m_name
                                       << "Result:" << success;
    return success;
}

bool YubiKeyCredentialProxy::typeCode(bool fallbackToCopy)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Cannot type code: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("TypeCode"), fallbackToCopy);

    if (!reply.isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "TypeCode failed for" << m_name
                                              << "Error:" << reply.error().message();
        return false;
    }

    bool success = reply.value();
    qCDebug(YubiKeyCredentialProxyLog) << "TypeCode for" << m_name
                                       << "Result:" << success
                                       << "Fallback to copy:" << fallbackToCopy;
    return success;
}

void YubiKeyCredentialProxy::deleteCredential()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Cannot delete credential: D-Bus interface invalid";
        return;
    }

    QDBusReply<void> reply = m_interface->call(QStringLiteral("Delete"));

    if (!reply.isValid()) {
        qCWarning(YubiKeyCredentialProxyLog) << "Delete failed for" << m_name
                                              << "Error:" << reply.error().message();
        return;
    }

    qCDebug(YubiKeyCredentialProxyLog) << "Deleted credential" << m_name;
}

CredentialInfo YubiKeyCredentialProxy::toCredentialInfo() const
{
    CredentialInfo info;
    info.name = m_name;
    info.issuer = m_issuer;
    info.username = m_username;
    info.requiresTouch = m_requiresTouch;
    info.validUntil = 0; // Not available in proxy (only in GenerateCodeResult)
    info.deviceId = m_deviceId;
    return info;
}

} // namespace Shared
} // namespace YubiKeyOath
