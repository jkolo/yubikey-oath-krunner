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

    // Connect to D-Bus signals
    connectToSignals();
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

// ========== Async Methods (fire-and-forget, results via signals) ==========

void OathCredentialProxy::generateCode()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot generate code: D-Bus interface invalid";
        Q_EMIT codeGenerated(QString(), 0, QStringLiteral("D-Bus interface invalid"));
        return;
    }

    // Call async method (NoReply annotation)
    m_interface->asyncCall(QStringLiteral("GenerateCode"));
    qCDebug(OathCredentialProxyLog) << "Requested async code generation for" << m_fullName;
}

void OathCredentialProxy::copyToClipboard()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot copy: D-Bus interface invalid";
        Q_EMIT clipboardCopied(false, QStringLiteral("D-Bus interface invalid"));
        return;
    }

    m_interface->asyncCall(QStringLiteral("CopyToClipboard"));
    qCDebug(OathCredentialProxyLog) << "Requested async clipboard copy for" << m_fullName;
}

void OathCredentialProxy::typeCode(bool fallbackToCopy)
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot type: D-Bus interface invalid";
        Q_EMIT codeTyped(false, QStringLiteral("D-Bus interface invalid"));
        return;
    }

    m_interface->asyncCall(QStringLiteral("TypeCode"), fallbackToCopy);
    qCDebug(OathCredentialProxyLog) << "Requested async code typing for" << m_fullName
                                       << "Fallback to copy:" << fallbackToCopy;
}

void OathCredentialProxy::deleteCredential()
{
    if (!m_interface || !m_interface->isValid()) {
        qCWarning(OathCredentialProxyLog) << "Cannot delete: D-Bus interface invalid";
        Q_EMIT deleted(false, QStringLiteral("D-Bus interface invalid"));
        return;
    }

    m_interface->asyncCall(QStringLiteral("Delete"));
    qCDebug(OathCredentialProxyLog) << "Requested async deletion for" << m_fullName;
}

// ========== Cache Getters ==========

GenerateCodeResult OathCredentialProxy::getCachedCode() const
{
    return GenerateCodeResult{.code = m_cachedCode, .validUntil = m_cachedValidUntil};
}

bool OathCredentialProxy::isCacheValid() const
{
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
    return !m_cachedCode.isEmpty() && m_cachedValidUntil > currentTime;
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

void OathCredentialProxy::connectToSignals()
{
    if (!m_interface || !m_interface->isValid()) {
        return;
    }

    // Connect to result signals
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("CodeGenerated"),
        this,
        SLOT(onCodeGenerated(QString,qint64,QString))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("ClipboardCopied"),
        this,
        SLOT(onClipboardCopied(bool,QString))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("CodeTyped"),
        this,
        SLOT(onCodeTyped(bool,QString))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("Deleted"),
        this,
        SLOT(onDeleted(bool,QString))
    );

    // Connect to workflow signals
    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("TouchRequired"),
        this,
        SLOT(onTouchRequired(int,QString))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("TouchCompleted"),
        this,
        SLOT(onTouchCompleted(bool))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("ReconnectRequired"),
        this,
        SLOT(onReconnectRequired(QString))
    );

    QDBusConnection::sessionBus().connect(
        QLatin1String(SERVICE_NAME),
        m_objectPath,
        QLatin1String(INTERFACE_NAME),
        QStringLiteral("ReconnectCompleted"),
        this,
        SLOT(onReconnectCompleted(bool))
    );
}

// ========== Signal Slots ==========

void OathCredentialProxy::onCodeGenerated(const QString &code, qint64 validUntil, const QString &error)
{
    qCDebug(OathCredentialProxyLog) << "CodeGenerated signal received for" << m_fullName
                                     << "Code length:" << code.length()
                                     << "ValidUntil:" << validUntil
                                     << "Error:" << error;

    // Update cache if successful
    if (error.isEmpty() && !code.isEmpty()) {
        m_cachedCode = code;
        m_cachedValidUntil = validUntil;
    }

    Q_EMIT codeGenerated(code, validUntil, error);
}

void OathCredentialProxy::onDeleted(bool success, const QString &error)
{
    qCDebug(OathCredentialProxyLog) << "Deleted signal received for" << m_fullName
                                     << "Success:" << success
                                     << "Error:" << error;

    Q_EMIT deleted(success, error);
}

void OathCredentialProxy::onClipboardCopied(bool success, const QString &error)
{
    qCDebug(OathCredentialProxyLog) << "Clipboard copied signal received for" << m_fullName
                                      << "success:" << success;
    Q_EMIT clipboardCopied(success, error);
}

void OathCredentialProxy::onCodeTyped(bool success, const QString &error)
{
    qCDebug(OathCredentialProxyLog) << "Code typed signal received for" << m_fullName
                                      << "success:" << success;
    Q_EMIT codeTyped(success, error);
}

void OathCredentialProxy::onTouchRequired(int timeoutSeconds, const QString &deviceModel)
{
    qCDebug(OathCredentialProxyLog) << "Touch required signal received for" << m_fullName
                                      << "timeout:" << timeoutSeconds << "s"
                                      << "device:" << deviceModel;
    Q_EMIT touchRequired(timeoutSeconds, deviceModel);
}

void OathCredentialProxy::onTouchCompleted(bool success)
{
    qCDebug(OathCredentialProxyLog) << "Touch completed signal received for" << m_fullName
                                      << "success:" << success;
    Q_EMIT touchCompleted(success);
}

void OathCredentialProxy::onReconnectRequired(const QString &deviceModel)
{
    qCDebug(OathCredentialProxyLog) << "Reconnect required signal received for" << m_fullName
                                      << "device:" << deviceModel;
    Q_EMIT reconnectRequired(deviceModel);
}

void OathCredentialProxy::onReconnectCompleted(bool success)
{
    qCDebug(OathCredentialProxyLog) << "Reconnect completed signal received for" << m_fullName
                                      << "success:" << success;
    Q_EMIT reconnectCompleted(success);
}

} // namespace Shared
} // namespace YubiKeyOath
