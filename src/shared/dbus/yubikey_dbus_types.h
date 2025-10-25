/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QMetaType>
#include <QDBusArgument>

namespace KRunner {
namespace YubiKey {

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
    QString name;               ///< Full credential name (issuer:username or just name)
    QString issuer;             ///< Issuer (extracted from name)
    QString username;           ///< Username (extracted from name)
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

} // namespace YubiKey
} // namespace KRunner

// Qt metatype registration
Q_DECLARE_METATYPE(KRunner::YubiKey::DeviceInfo)
Q_DECLARE_METATYPE(KRunner::YubiKey::CredentialInfo)
Q_DECLARE_METATYPE(KRunner::YubiKey::GenerateCodeResult)
Q_DECLARE_METATYPE(KRunner::YubiKey::AddCredentialResult)

// D-Bus marshaling operators
QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::DeviceInfo &device);
const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::DeviceInfo &device);

QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::CredentialInfo &cred);
const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::CredentialInfo &cred);

QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::GenerateCodeResult &result);
const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::GenerateCodeResult &result);

QDBusArgument &operator<<(QDBusArgument &arg, const KRunner::YubiKey::AddCredentialResult &result);
const QDBusArgument &operator>>(const QDBusArgument &arg, KRunner::YubiKey::AddCredentialResult &result);
