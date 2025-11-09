/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_value_types.h"

// DeviceInfo marshaling
// D-Bus signature: (ssusassbbbt)
// - s: deviceName (QString)
// - s: firmwareVersion (Version → QString via operator<<)
// - u: serialNumber (quint32)
// - s: deviceModel (QString, human-readable)
// - as: capabilities (QStringList)
// - s: formFactor (QString, human-readable)
// - b: isConnected (bool)
// - b: requiresPassword (bool)
// - b: hasValidPassword (bool)
// - x: lastSeen (qint64, Unix timestamp in milliseconds)
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::DeviceInfo &device)
{
    arg.beginStructure();
    arg << device.deviceName
        << device.firmwareVersion
        << device.serialNumber
        << device.deviceModel
        << device.capabilities
        << device.formFactor
        << device.isConnected
        << device.requiresPassword
        << device.hasValidPassword
        << device.lastSeen.toMSecsSinceEpoch();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::DeviceInfo &device)
{
    qint64 lastSeenMsecs = 0;
    arg.beginStructure();
    arg >> device.deviceName
        >> device.firmwareVersion
        >> device.serialNumber
        >> device.deviceModel
        >> device.capabilities
        >> device.formFactor
        >> device.isConnected
        >> device.requiresPassword
        >> device.hasValidPassword
        >> lastSeenMsecs;
    arg.endStructure();
    device.lastSeen = QDateTime::fromMSecsSinceEpoch(lastSeenMsecs);
    return arg;
}

// CredentialInfo marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::CredentialInfo &cred)
{
    arg.beginStructure();
    arg << cred.name
        << cred.issuer
        << cred.account
        << cred.requiresTouch
        << cred.validUntil
        << cred.deviceId;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::CredentialInfo &cred)
{
    arg.beginStructure();
    arg >> cred.name
        >> cred.issuer
        >> cred.account
        >> cred.requiresTouch
        >> cred.validUntil
        >> cred.deviceId;
    arg.endStructure();
    return arg;
}

// GenerateCodeResult marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::GenerateCodeResult &result)
{
    arg.beginStructure();
    arg << result.code << result.validUntil;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::GenerateCodeResult &result)
{
    arg.beginStructure();
    arg >> result.code >> result.validUntil;
    arg.endStructure();
    return arg;
}

// AddCredentialResult marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::AddCredentialResult &result)
{
    arg.beginStructure();
    arg << result.status << result.message;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::AddCredentialResult &result)
{
    arg.beginStructure();
    arg >> result.status >> result.message;
    arg.endStructure();
    return arg;
}
