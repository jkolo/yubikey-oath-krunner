/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QMetaType>
#include <QDBusArgument>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Information about a YubiKey device for D-Bus transfer
 */
struct DeviceInfo {
    QString deviceId;           ///< Unique device identifier (hex string)
    QString deviceName;         ///< Friendly name
    bool isConnected;           ///< Currently connected via PC/SC
    bool requiresPassword;      ///< Device requires password for OATH access
    bool hasValidPassword;      ///< We have a valid password stored
};

/**
 * @brief Information about an OATH credential for D-Bus transfer
 */
struct CredentialInfo {
    QString name;               ///< Full credential name (issuer:account or just account)
    QString issuer;             ///< Issuer (extracted from name)
    QString account;            ///< Account/username (extracted from name)
    bool requiresTouch;         ///< Requires physical touch to generate code
    qint64 validUntil;          ///< Unix timestamp when code expires (0 if touch required)
    QString deviceId;           ///< Device ID identifying which YubiKey has this credential
};

/**
 * @brief Result of generating a TOTP/HOTP code
 */
struct GenerateCodeResult {
    QString code;               ///< Generated code (6-8 digits)
    qint64 validUntil;          ///< Unix timestamp when code expires
};

/**
 * @brief Result of adding a credential to YubiKey
 */
struct AddCredentialResult {
    QString status;             ///< Status: "Success", "Interactive", "Error"
    QString message;            ///< Success/error message or empty string

    AddCredentialResult() = default;
    AddCredentialResult(const QString &s, const QString &m = QString())
        : status(s), message(m) {}
};

} // namespace Shared
} // namespace YubiKeyOath

// Qt metatype registration
Q_DECLARE_METATYPE(YubiKeyOath::Shared::DeviceInfo)
Q_DECLARE_METATYPE(YubiKeyOath::Shared::CredentialInfo)
Q_DECLARE_METATYPE(YubiKeyOath::Shared::GenerateCodeResult)
Q_DECLARE_METATYPE(YubiKeyOath::Shared::AddCredentialResult)

// D-Bus marshaling operators
QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::DeviceInfo &device);
const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::DeviceInfo &device);

QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::CredentialInfo &cred);
const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::CredentialInfo &cred);

QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::GenerateCodeResult &result);
const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::GenerateCodeResult &result);

QDBusArgument &operator<<(QDBusArgument &arg, const YubiKeyOath::Shared::AddCredentialResult &result);
const QDBusArgument &operator>>(const QDBusArgument &arg, YubiKeyOath::Shared::AddCredentialResult &result);
