/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef YUBIKEY_OATH_PASSWORD_DERIVATION_H
#define YUBIKEY_OATH_PASSWORD_DERIVATION_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Password derivation utilities for OATH authentication.
 *
 * Implements PBKDF2 (Password-Based Key Derivation Function 2) as specified
 * in RFC 8018 section 5.2. Used for deriving encryption keys from passwords
 * for YubiKey OATH application authentication.
 */
namespace PasswordDerivation {

/// OATH specification PBKDF2 iteration count
constexpr int OATH_PBKDF2_ITERATIONS = 1000;

/// OATH specification derived key length in bytes (128-bit AES key)
constexpr int OATH_DERIVED_KEY_LENGTH = 16;

/**
 * @brief Derives a key from password using PBKDF2-HMAC-SHA1.
 *
 * @param password The password bytes (typically UTF-8 encoded)
 * @param salt The salt value (typically device ID in hex)
 * @param iterations Number of PBKDF2 iterations (typically 1000 for OATH)
 * @param keyLength Desired key length in bytes (typically 16 for OATH)
 * @return Derived key bytes
 *
 * @note This implementation uses HMAC-SHA1 as the PRF, producing 20-byte
 * blocks. For keys longer than 20 bytes, multiple blocks are concatenated.
 */
inline QByteArray deriveKeyPbkdf2(const QByteArray &password,
                                   const QByteArray &salt,
                                   int iterations,
                                   int keyLength)
{
    QByteArray derivedKey;
    const int blockCount = (keyLength + 19) / 20; // SHA1 produces 20 bytes per block

    for (int block = 1; block <= blockCount; ++block) {
        // Create block salt: salt || INT(block) - big-endian 32-bit integer
        QByteArray blockSalt = salt;
        blockSalt.append(static_cast<char>((block >> 24) & 0xFF));
        blockSalt.append(static_cast<char>((block >> 16) & 0xFF));
        blockSalt.append(static_cast<char>((block >> 8) & 0xFF));
        blockSalt.append(static_cast<char>(block & 0xFF));

        // U1 = PRF(password, salt || INT(block))
        QByteArray u = QMessageAuthenticationCode::hash(blockSalt, password, QCryptographicHash::Sha1);
        QByteArray result = u;

        // U2..Uc = PRF(password, U{c-1})
        for (int i = 1; i < iterations; ++i) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha1);

            // XOR with result
            for (int j = 0; j < u.length(); ++j) {
                result[j] = static_cast<char>(result[j] ^ u[j]);
            }
        }

        derivedKey.append(result);
    }

    return derivedKey.left(keyLength);
}

} // namespace PasswordDerivation
} // namespace Daemon
} // namespace YubiKeyOath

#endif // YUBIKEY_OATH_PASSWORD_DERIVATION_H
