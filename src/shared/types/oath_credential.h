/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QList>
#include <QMetaType>
#include <QDebug>
#include <QDataStream>

namespace YubiKeyOath {
namespace Shared {

// Forward declare enums from oath_credential_data.h
enum class OathType;
enum class OathAlgorithm;

/**
 * @brief Represents a YubiKey OATH credential
 */
struct OathCredential {
    QString name;              ///< Full credential name
    QString issuer;            ///< Service issuer
    QString username;          ///< Username/account
    QString code;              ///< Generated TOTP/HOTP code
    qint64 validUntil = 0;     ///< Code validity timestamp
    bool requiresTouch = false; ///< Whether credential requires physical touch
    bool isTotp = true;        ///< Whether this is TOTP (true) or HOTP (false)
    QString deviceId;          ///< Device ID (for multi-device support, not serialized)

    // Extended metadata (optional, for D-Bus properties)
    int digits = 6;            ///< Number of digits (6-8)
    int period = 30;           ///< TOTP period in seconds
    int algorithm = 1;         ///< Algorithm: 1=SHA1, 2=SHA256, 3=SHA512
    int type = 2;              ///< Type: 1=HOTP, 2=TOTP
};

// QDebug operator for debug output
inline QDebug operator<<(QDebug debug, const OathCredential &cred) {
    QDebugStateSaver saver(debug);
    debug.nospace() << "OathCredential(" << cred.name << ")";
    return debug;
}

// QDataStream operators for serialization
inline QDataStream &operator<<(QDataStream &out, const OathCredential &cred) {
    out << cred.name << cred.issuer << cred.username << cred.code
        << cred.validUntil << cred.requiresTouch << cred.isTotp
        << cred.digits << cred.period << cred.algorithm << cred.type;
    return out;
}

inline QDataStream &operator>>(QDataStream &in, OathCredential &cred) {
    in >> cred.name >> cred.issuer >> cred.username >> cred.code
       >> cred.validUntil >> cred.requiresTouch >> cred.isTotp
       >> cred.digits >> cred.period >> cred.algorithm >> cred.type;
    return in;
}

} // namespace Shared
} // namespace YubiKeyOath

// Register with Qt meta-type system for use in signals/slots
Q_DECLARE_METATYPE(YubiKeyOath::Shared::OathCredential)
Q_DECLARE_METATYPE(QList<YubiKeyOath::Shared::OathCredential>)
