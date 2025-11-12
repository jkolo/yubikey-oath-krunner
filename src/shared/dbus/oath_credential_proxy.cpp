/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_credential_proxy.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QDateTime>

Q_LOGGING_CATEGORY(OathCredentialProxyLog, "pl.jkolo.yubikey.oath.daemon.credential.proxy")

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

OathCredentialProxy::OathCredentialProxy(const QString &objectPath,
                                               const QVariantMap &properties,
                                               QObject *parent)
    : QObject(parent)
    , m_objectPath(objectPath)
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
        qCWarning(OathCredentialProxyLog) << "Failed to create D-Bus interface for credential at"
                                              << objectPath
                                              << "Error:" << m_interface->lastError().message();
    }

    // Extract and cache properties from GetManagedObjects() result
    m_fullName = properties.value(QStringLiteral("FullName")).toString();
    m_issuer = properties.value(QStringLiteral("Issuer")).toString();
    m_username = properties.value(QStringLiteral("Username")).toString();
    m_requiresTouch = properties.value(QStringLiteral("RequiresTouch")).toBool();
    m_type = properties.value(QStringLiteral("Type")).toString();
    m_algorithm = properties.value(QStringLiteral("Algorithm")).toString();
    m_digits = properties.value(QStringLiteral("Digits")).toInt();
    m_period = properties.value(QStringLiteral("Period")).toInt();
    m_deviceId = properties.value(QStringLiteral("DeviceId")).toString();

    qCDebug(OathCredentialProxyLog) << "Created credential proxy for" << m_fullName
                                       << "at" << objectPath;
}

OathCredentialProxy::~OathCredentialProxy()
{
    qCDebug(OathCredentialProxyLog) << "Destroying credential proxy for" << m_fullName;
}

QString OathCredentialProxy::parentDeviceId() const
{
    // Extract device ID from object path
    // Path format: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
    // Segments:     0   1     2       3    4       5           6            7
    // We want segment 6 (0-indexed from root)
    return m_objectPath.section(QLatin1Char('/'), 6, 6);
}

GenerateCodeResult OathCredentialProxy::generateCode()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot generate code: D-Bus interface invalid";
        return GenerateCodeResult{.code = QString(), .validUntil = 0};
    }

    // PERFORMANCE: Check cache before calling D-Bus
    // This eliminates N separate D-Bus calls when building KRunner matches
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();

    if (!m_cachedCode.isEmpty() && m_cachedValidUntil > currentTime) {
        qCDebug(OathCredentialProxyLog) << "Returning cached code for" << m_fullName
                                           << "Valid for" << (m_cachedValidUntil - currentTime) << "more seconds";
        return GenerateCodeResult{.code = m_cachedCode, .validUntil = m_cachedValidUntil};
    }

    // Cache miss or expired - call D-Bus
    qCDebug(OathCredentialProxyLog) << "Cache miss/expired for" << m_fullName << "- calling D-Bus";
    QDBusReply<GenerateCodeResult> reply = m_interface->call(QStringLiteral("GenerateCode"));

    if (!reply.isValid()) {
        qCWarning(OathCredentialProxyLog) << "GenerateCode failed for" << m_fullName
                                              << "Error:" << reply.error().message();
        // Don't clear cache on error - return old cached code if available
        if (!m_cachedCode.isEmpty()) {
            qCWarning(OathCredentialProxyLog) << "Returning stale cached code due to D-Bus error";
            return GenerateCodeResult{.code = m_cachedCode, .validUntil = m_cachedValidUntil};
        }
        return GenerateCodeResult{.code = QString(), .validUntil = 0};
    }

    auto result = reply.value();
    qCDebug(OathCredentialProxyLog) << "Generated code for" << m_fullName
                                       << "Valid until:" << result.validUntil;

    // Update cache
    m_cachedCode = result.code;
    m_cachedValidUntil = result.validUntil;

    return result;
}

bool OathCredentialProxy::copyToClipboard()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot copy to clipboard: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("CopyToClipboard"));

    if (!reply.isValid()) {
        qCWarning(OathCredentialProxyLog) << "CopyToClipboard failed for" << m_fullName
                                              << "Error:" << reply.error().message();
        return false;
    }

    bool const success = reply.value();
    qCDebug(OathCredentialProxyLog) << "CopyToClipboard for" << m_fullName
                                       << "Result:" << success;
    return success;
}

bool OathCredentialProxy::typeCode(bool fallbackToCopy)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot type code: D-Bus interface invalid";
        return false;
    }

    QDBusReply<bool> reply = m_interface->call(QStringLiteral("TypeCode"), fallbackToCopy);

    if (!reply.isValid()) {
        qCWarning(OathCredentialProxyLog) << "TypeCode failed for" << m_fullName
                                              << "Error:" << reply.error().message();
        return false;
    }

    bool const success = reply.value();
    qCDebug(OathCredentialProxyLog) << "TypeCode for" << m_fullName
                                       << "Result:" << success
                                       << "Fallback to copy:" << fallbackToCopy;
    return success;
}

void OathCredentialProxy::deleteCredential()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot delete credential: D-Bus interface invalid";
        return;
    }

    QDBusReply<void> reply = m_interface->call(QStringLiteral("Delete"));

    if (!reply.isValid()) {
        qCWarning(OathCredentialProxyLog) << "Delete failed for" << m_fullName
                                              << "Error:" << reply.error().message();
        return;
    }

    qCDebug(OathCredentialProxyLog) << "Deleted credential" << m_fullName;
}

CredentialInfo OathCredentialProxy::toCredentialInfo() const
{
    CredentialInfo info;
    info.name = m_fullName;
    info.issuer = m_issuer;
    info.account = m_username;
    info.requiresTouch = m_requiresTouch;
    info.validUntil = 0; // Not available in proxy (only in GenerateCodeResult)
    info.deviceId = m_deviceId;
    return info;
}

} // namespace Shared
} // namespace YubiKeyOath
