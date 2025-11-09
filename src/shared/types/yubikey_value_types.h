/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef YUBIKEY_VALUE_TYPES_H
#define YUBIKEY_VALUE_TYPES_H

#include <QString>
#include <utility>
#include <QMetaType>
#include <QDBusArgument>
#include <QDateTime>
#include "../utils/version.h"
#include "yubikey_model.h"

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Information about a YubiKey device for D-Bus transfer
 */
struct DeviceInfo {
    QString deviceName;         ///< Friendly name
    Version firmwareVersion;    ///< Firmware version (e.g., 5.4.3 or 3.4.0)
    quint32 serialNumber{0};    ///< Device serial number (0 if unavailable)
    QString deviceModel;        ///< Human-readable model (e.g., "YubiKey 5C NFC")
    YubiKeyModel deviceModelCode{0}; ///< Numeric model code (0xSSVVPPFF format) for icon resolution
    QStringList capabilities;   ///< List of capabilities (e.g., ["FIDO2", "OATH-TOTP", "PIV"])
    QString formFactor;         ///< Human-readable form factor (e.g., "USB-A Keychain")
    bool isConnected{false};    ///< Currently connected via PC/SC
    bool requiresPassword{false}; ///< Device requires password for OATH access
    bool hasValidPassword{false}; ///< We have a valid password stored
    QDateTime lastSeen;         ///< Last time device was seen (for offline devices)

    // Internal fields (not exported via D-Bus, used only within daemon for identification)
    QString _internalDeviceId;  ///< Internal device ID (hex string) - NOT serialized to D-Bus
};

/**
 * @brief Information about an OATH credential for D-Bus transfer
 */
struct CredentialInfo {
    QString name;               ///< Full credential name (issuer:account or just account)
    QString issuer;             ///< Issuer (extracted from name)
    QString account;            ///< Account/username (extracted from name)
    bool requiresTouch{false};  ///< Requires physical touch to generate code
    qint64 validUntil{0};       ///< Unix timestamp when code expires (0 if touch required)
    QString deviceId;           ///< Device ID identifying which YubiKey has this credential
};

/**
 * @brief Result of generating a TOTP/HOTP code
 */
struct GenerateCodeResult {
    QString code;               ///< Generated code (6-8 digits)
    qint64 validUntil{0};       ///< Unix timestamp when code expires
};

/**
 * @brief Result of adding a credential to YubiKey
 */
struct AddCredentialResult {
    QString status;             ///< Status: "Success", "Interactive", "Error"
    QString message;            ///< Success/error message or empty string

    AddCredentialResult() = default;
    AddCredentialResult(QString s, QString m = QString())
        : status(std::move(s)), message(std::move(m)) {}
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

#endif // YUBIKEY_VALUE_TYPES_H
