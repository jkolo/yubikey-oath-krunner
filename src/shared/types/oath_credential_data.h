/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QMetaType>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief OATH algorithm types
 */
enum class OathAlgorithm {
    SHA1 = 0x01,
    SHA256 = 0x02,
    SHA512 = 0x03
};

/**
 * @brief OATH credential type
 */
enum class OathType {
    HOTP = 0x01,
    TOTP = 0x02
};

/**
 * @brief Data for adding/updating OATH credential on YubiKey
 *
 * This structure contains all parameters needed for the PUT command
 * to add a new credential to YubiKey OATH application.
 */
struct OathCredentialData {
    QString name;                   ///< Full credential name (issuer:account)
    QString issuer;                 ///< Service issuer (e.g., "Google")
    QString account;                ///< Account/username (e.g., "user@example.com")
    QString secret;                 ///< Base32-encoded secret key
    OathType type = OathType::TOTP; ///< TOTP or HOTP
    OathAlgorithm algorithm = OathAlgorithm::SHA1; ///< Hash algorithm
    int digits = 6;                 ///< Number of digits (6-8)
    int period = 30;                ///< TOTP period in seconds (default 30)
    quint32 counter = 0;            ///< HOTP initial counter value
    bool requireTouch = false;      ///< Require physical touch for code generation

    /**
     * @brief Validates credential data
     * @return Error message if invalid, empty string if valid
     */
    QString validate() const {
        if (name.isEmpty()) {
            return i18n("Credential name is required");
        }

        if (secret.isEmpty()) {
            return i18n("Secret is required");
        }

        // Validate Base32 format (A-Z, 2-7, optional padding =)
        static const QString validBase32Chars = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567=");
        for (const QChar &ch : secret) {
            if (!validBase32Chars.contains(ch.toUpper())) {
                return i18n("Secret must be valid Base32 (A-Z, 2-7)");
            }
        }

        if (digits < 6 || digits > 8) {
            return i18n("Digits must be 6, 7, or 8");
        }

        if (type == OathType::TOTP) {
            if (period <= 0) {
                return i18n("Period must be positive");
            }
        }

        return QString(); // Valid
    }

    /**
     * @brief Gets full credential name in format "issuer:account"
     * @return Formatted name
     */
    QString fullName() const {
        if (issuer.isEmpty()) {
            return account;
        }
        return issuer + QStringLiteral(":") + account;
    }
};

// QDebug operator for debug output
inline QDebug operator<<(QDebug debug, const OathCredentialData &data) {
    QDebugStateSaver saver(debug);
    debug.nospace() << "OathCredentialData("
                    << "name=" << data.name
                    << ", issuer=" << data.issuer
                    << ", account=" << data.account
                    << ", type=" << (data.type == OathType::TOTP ? "TOTP" : "HOTP")
                    << ", algorithm=" << static_cast<int>(data.algorithm)
                    << ", digits=" << data.digits
                    << ", period=" << data.period
                    << ", counter=" << data.counter
                    << ", requireTouch=" << data.requireTouch
                    << ")";
    return debug;
}

// Helper functions for algorithm conversion
inline QString algorithmToString(OathAlgorithm algo) {
    switch (algo) {
        case OathAlgorithm::SHA1:   return QStringLiteral("SHA1");
        case OathAlgorithm::SHA256: return QStringLiteral("SHA256");
        case OathAlgorithm::SHA512: return QStringLiteral("SHA512");
        default: return QStringLiteral("SHA1");
    }
}

inline OathAlgorithm algorithmFromString(const QString &str) {
    QString upper = str.toUpper();
    if (upper == QStringLiteral("SHA256")) return OathAlgorithm::SHA256;
    if (upper == QStringLiteral("SHA512")) return OathAlgorithm::SHA512;
    return OathAlgorithm::SHA1; // Default
}

} // namespace YubiKey
} // namespace KRunner

// Register with Qt meta-type system for use in signals/slots
Q_DECLARE_METATYPE(KRunner::YubiKey::OathCredentialData)
Q_DECLARE_METATYPE(KRunner::YubiKey::OathAlgorithm)
Q_DECLARE_METATYPE(KRunner::YubiKey::OathType)
