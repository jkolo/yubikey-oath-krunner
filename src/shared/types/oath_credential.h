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
#include "oath_credential_data.h"

namespace YubiKeyOath {
namespace Shared {

// Forward declarations for formatting options
struct FormatOptions;

/**
 * @brief Represents a YubiKey OATH credential with business logic
 *
 * This is a rich domain model (not anemic) that encapsulates both data and behavior.
 * Moved formatting and validation logic from utility classes to follow "Tell, Don't Ask" principle.
 */
struct OathCredential {
    QString originalName;      ///< Full name as stored in YubiKey WITH period if non-standard ([period/]issuer:account)
    QString issuer;            ///< Service issuer
    QString account;           ///< Account/username
    QString code;              ///< Generated TOTP/HOTP code
    qint64 validUntil = 0;     ///< Code validity timestamp
    bool requiresTouch = false; ///< Whether credential requires physical touch
    bool isTotp = true;        ///< Whether this is TOTP (true) or HOTP (false)
    QString deviceId;          ///< Device ID (for multi-device support, not serialized)

    // Extended metadata (optional, for D-Bus properties)
    int digits = 6;            ///< Number of digits (6-8)
    int period = 30;           ///< TOTP period in seconds
    OathAlgorithm algorithm = OathAlgorithm::SHA1;   ///< Hash algorithm
    OathType type = OathType::TOTP;                  ///< Credential type

    /**
     * @brief Formats credential for display with flexible options
     *
     * Encapsulates display formatting logic that was previously in CredentialFormatter.
     * Follows "Tell, Don't Ask" - the credential knows how to display itself.
     *
     * @param options Formatting options (username, code, device name visibility)
     * @return Formatted display string
     *
     * @par Example formats:
     * - Minimal: "Google"
     * - With username: "Google (user@example.com)"
     * - With code: "Google (user@example.com) - 123456"
     * - Touch required: "Google (user@example.com) - 👆"
     * - With device: "Google @ YubiKey 5"
     *
     * @note Thread-safe: Can be called from any thread
     * @note For touch-required credentials, code is never shown even if showCode is true
     */
    QString getDisplayName(const FormatOptions &options) const;

    /**
     * @brief Formats credential with explicit code and touch status
     *
     * Similar to getDisplayName(), but uses explicit code and touch parameters.
     * Used when we already generated the code or know touch status separately.
     *
     * @param explicitCode Generated TOTP/HOTP code (overrides this->code)
     * @param explicitRequiresTouch Touch requirement (overrides this->requiresTouch)
     * @param options Formatting options
     * @return Formatted display string
     *
     * @note Thread-safe
     * @note When showCode=true and explicitRequiresTouch=true, displays 👆 emoji
     */
    QString getDisplayNameWithCode(const QString &explicitCode,
                                     bool explicitRequiresTouch,
                                     const FormatOptions &options) const;

    /**
     * @brief Checks if credential matches name and device ID
     *
     * Encapsulates matching logic that was in Utils::findCredential().
     * Exact name comparison (case-sensitive).
     *
     * @param name Credential name to match
     * @param targetDeviceId Device ID to match
     * @return true if both name and device ID match
     *
     * @note Thread-safe
     */
    bool matches(const QString &name, const QString &targetDeviceId) const;

    /**
     * @brief Checks if TOTP code has expired
     *
     * Compares validUntil timestamp with current time.
     *
     * @return true if code expired (validUntil in past)
     * @note Always returns false for HOTP credentials (they don't expire)
     * @note Thread-safe (uses QDateTime::currentSecsSinceEpoch())
     */
    bool isExpired() const;

    /**
     * @brief Checks if TOTP code needs regeneration soon
     *
     * Returns true if code will expire within threshold seconds.
     * Useful for proactive code regeneration before user sees expired code.
     *
     * @param thresholdSeconds Seconds before expiration to trigger (default: 5)
     * @return true if code expires within threshold
     * @note Always returns false for HOTP credentials
     * @note Thread-safe
     */
    bool needsRegeneration(int thresholdSeconds = 5) const;
};

// QDebug operator for debug output
inline QDebug operator<<(QDebug debug, const OathCredential &cred) {
    QDebugStateSaver saver(debug);
    debug.nospace() << "OathCredential(" << cred.originalName << ")";
    return debug;
}

// QDataStream operators for serialization
inline QDataStream &operator<<(QDataStream &out, const OathCredential &cred) {
    out << cred.originalName << cred.issuer << cred.account << cred.code
        << cred.validUntil << cred.requiresTouch << cred.isTotp
        << cred.digits << cred.period
        << static_cast<int>(cred.algorithm) << static_cast<int>(cred.type);
    return out;
}

inline QDataStream &operator>>(QDataStream &in, OathCredential &cred) {
    int algorithm = 0;
    int type = 0;
    in >> cred.originalName >> cred.issuer >> cred.account >> cred.code
       >> cred.validUntil >> cred.requiresTouch >> cred.isTotp
       >> cred.digits >> cred.period >> algorithm >> type;
    cred.algorithm = static_cast<OathAlgorithm>(algorithm);
    cred.type = static_cast<OathType>(type);
    return in;
}

} // namespace Shared
} // namespace YubiKeyOath

// Register with Qt meta-type system for use in signals/slots
Q_DECLARE_METATYPE(YubiKeyOath::Shared::OathCredential)
Q_DECLARE_METATYPE(QList<YubiKeyOath::Shared::OathCredential>)
