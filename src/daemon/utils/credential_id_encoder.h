/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Utility class for encoding credential names for D-Bus object paths
 *
 * D-Bus object paths have strict character requirements: [A-Za-z0-9_/]
 * This class handles transliteration of Unicode characters and encoding
 * of special characters to produce valid D-Bus path elements.
 *
 * @par Transliteration
 * Polish characters (ąćęłńóśźż, ĄĆĘŁŃÓŚŹŻ) are transliterated to ASCII equivalents.
 * Common special characters (@.:-+=/&#%!? etc.) are mapped to readable names.
 * Other Unicode characters are encoded as _uXXXX.
 *
 * @par Examples
 * - "GitHub:user@example.com" → "github_colon_user_at_example_dot_com"
 * - "Żółć" → "zolc"
 * - "123service" → "c123service" (prepended 'c' for digits)
 *
 * @note Very long names (>200 chars) are truncated and hashed.
 */
class CredentialIdEncoder
{
public:
    /**
     * @brief Encodes credential name for use in D-Bus object path
     * @param credentialName Full credential name (issuer:account or just account)
     * @return Encoded ID suitable for D-Bus object path element
     *
     * The result:
     * - Contains only [a-z0-9_]
     * - Does not start with a digit (prepended 'c' if necessary)
     * - Maximum 200 characters (longer names are hashed)
     */
    [[nodiscard]] static QString encode(const QString &credentialName);

private:
    CredentialIdEncoder() = delete;  // Pure utility class
};

} // namespace Daemon
} // namespace YubiKeyOath
