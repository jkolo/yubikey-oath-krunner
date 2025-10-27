/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_credential_object.h"
#include "services/yubikey_service.h"
#include "logging_categories.h"

#include <QDBusConnection>
#include <QDBusError>

namespace YubiKeyOath {
namespace Daemon {

static constexpr const char *CREDENTIAL_INTERFACE = "pl.jkolo.yubikey.oath.Credential";

YubiKeyCredentialObject::YubiKeyCredentialObject(const Shared::OathCredential &credential,
                                                 const QString &deviceId,
                                                 YubiKeyService *service,
                                                 const QDBusConnection &connection,
                                                 QObject *parent)
    : QObject(parent)
    , m_credential(credential)
    , m_deviceId(deviceId)
    , m_service(service)
    , m_connection(connection)
    , m_registered(false)
{
    // Object path will be set by parent (YubiKeyDeviceObject)
    // For now, construct it here for logging
    // Format: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
    // credentialId is encoded by parent

    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Constructing for credential:"
                              << m_credential.name << "on device:" << m_deviceId;
}

YubiKeyCredentialObject::~YubiKeyCredentialObject()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Destructor for credential:"
                              << m_credential.name;
    unregisterObject();
}

void YubiKeyCredentialObject::setObjectPath(const QString &path)
{
    if (m_registered) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Cannot change path after registration";
        return;
    }
    m_objectPath = path;
}

bool YubiKeyCredentialObject::registerObject()
{
    if (m_registered) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Already registered:"
                                    << m_credential.name;
        return true;
    }

    // Object path must be set by parent before calling registerObject()
    if (m_objectPath.isEmpty()) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Cannot register - no object path set";
        return false;
    }

    // Register on D-Bus
    if (!m_connection.registerObject(m_objectPath, this,
                                     QDBusConnection::ExportAllProperties |
                                     QDBusConnection::ExportAllSlots |
                                     QDBusConnection::ExportAllSignals)) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Failed to register at"
                                     << m_objectPath << ":" << m_connection.lastError().message();
        return false;
    }

    m_registered = true;
    qCInfo(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Registered successfully:"
                             << m_credential.name << "at" << m_objectPath;

    return true;
}

void YubiKeyCredentialObject::unregisterObject()
{
    if (!m_registered) {
        return;
    }

    m_connection.unregisterObject(m_objectPath);
    m_registered = false;
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Unregistered:" << m_credential.name;
}

QString YubiKeyCredentialObject::objectPath() const
{
    return m_objectPath;
}

QString YubiKeyCredentialObject::name() const
{
    return m_credential.name;
}

QString YubiKeyCredentialObject::issuer() const
{
    return m_credential.issuer;
}

QString YubiKeyCredentialObject::username() const
{
    return m_credential.username;
}

bool YubiKeyCredentialObject::requiresTouch() const
{
    return m_credential.requiresTouch;
}

QString YubiKeyCredentialObject::type() const
{
    // Type: 1=HOTP, 2=TOTP
    return m_credential.type == 2
           ? QString::fromLatin1("TOTP")
           : QString::fromLatin1("HOTP");
}

QString YubiKeyCredentialObject::algorithm() const
{
    // Algorithm: 1=SHA1, 2=SHA256, 3=SHA512
    switch (m_credential.algorithm) {
        case 2:
            return QString::fromLatin1("SHA256");
        case 3:
            return QString::fromLatin1("SHA512");
        case 1:
        default:
            return QString::fromLatin1("SHA1");
    }
}

int YubiKeyCredentialObject::digits() const
{
    return m_credential.digits;
}

int YubiKeyCredentialObject::period() const
{
    return m_credential.period;
}

QString YubiKeyCredentialObject::deviceId() const
{
    return m_deviceId;
}

Shared::GenerateCodeResult YubiKeyCredentialObject::GenerateCode()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: GenerateCode for credential:"
                              << m_credential.name << "on device:" << m_deviceId;

    return m_service->generateCode(m_deviceId, m_credential.name);
}

bool YubiKeyCredentialObject::CopyToClipboard()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: CopyToClipboard for credential:"
                              << m_credential.name << "on device:" << m_deviceId;

    return m_service->copyCodeToClipboard(m_deviceId, m_credential.name);
}

bool YubiKeyCredentialObject::TypeCode(bool fallbackToCopy)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: TypeCode for credential:"
                              << m_credential.name << "on device:" << m_deviceId
                              << "fallbackToCopy:" << fallbackToCopy;

    const bool success = m_service->typeCode(m_deviceId, m_credential.name);

    // If typing failed and fallback is enabled, try clipboard
    if (!success && fallbackToCopy) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: TypeCode failed, falling back to clipboard";
        return m_service->copyCodeToClipboard(m_deviceId, m_credential.name);
    }

    return success;
}

void YubiKeyCredentialObject::Delete()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Delete credential:"
                              << m_credential.name << "from device:" << m_deviceId;

    // TODO: Implement DeleteCredential in YubiKeyService
    // For now, just log warning
    qCWarning(YubiKeyDaemonLog) << "YubiKeyCredentialObject: Delete not yet implemented";
}

QVariantMap YubiKeyCredentialObject::getManagedObjectData() const
{
    QVariantMap result;

    // pl.jkolo.yubikey.oath.Credential interface properties
    QVariantMap credProps;
    credProps.insert(QLatin1String("Name"), m_credential.name);
    credProps.insert(QLatin1String("Issuer"), m_credential.issuer);
    credProps.insert(QLatin1String("Username"), m_credential.username);
    credProps.insert(QLatin1String("RequiresTouch"), m_credential.requiresTouch);
    credProps.insert(QLatin1String("Type"), type());
    credProps.insert(QLatin1String("Algorithm"), algorithm());
    credProps.insert(QLatin1String("Digits"), m_credential.digits);
    credProps.insert(QLatin1String("Period"), m_credential.period);
    credProps.insert(QLatin1String("DeviceId"), m_deviceId);

    result.insert(QLatin1String(CREDENTIAL_INTERFACE), credProps);

    return result;
}

} // namespace Daemon
} // namespace YubiKeyOath
