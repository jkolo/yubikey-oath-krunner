/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_value_types.h"

// DeviceInfo marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::DeviceInfo &device)
{
    arg.beginStructure();
    arg << device.deviceId
        << device.deviceName
        << device.isConnected
        << device.requiresPassword
        << device.hasValidPassword;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::DeviceInfo &device)
{
    arg.beginStructure();
    arg >> device.deviceId
        >> device.deviceName
        >> device.isConnected
        >> device.requiresPassword
        >> device.hasValidPassword;
    arg.endStructure();
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
