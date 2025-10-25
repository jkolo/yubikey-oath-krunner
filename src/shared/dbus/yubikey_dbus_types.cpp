/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_types.h"

// DeviceInfo marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::DeviceInfo &device)
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

const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::DeviceInfo &device)
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
QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::CredentialInfo &cred)
{
    arg.beginStructure();
    arg << cred.name
        << cred.issuer
        << cred.username
        << cred.requiresTouch
        << cred.validUntil
        << cred.deviceId;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::CredentialInfo &cred)
{
    arg.beginStructure();
    arg >> cred.name
        >> cred.issuer
        >> cred.username
        >> cred.requiresTouch
        >> cred.validUntil
        >> cred.deviceId;
    arg.endStructure();
    return arg;
}

// GenerateCodeResult marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::GenerateCodeResult &result)
{
    arg.beginStructure();
    arg << result.code << result.validUntil;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::GenerateCodeResult &result)
{
    arg.beginStructure();
    arg >> result.code >> result.validUntil;
    arg.endStructure();
    return arg;
}

// AddCredentialResult marshaling
QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::AddCredentialResult &result)
{
    arg.beginStructure();
    arg << result.status << result.message;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::AddCredentialResult &result)
{
    arg.beginStructure();
    arg >> result.status >> result.message;
    arg.endStructure();
    return arg;
}
