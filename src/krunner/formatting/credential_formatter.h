/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../shared/types/oath_credential.h"
#include "../../shared/dbus/yubikey_dbus_types.h"
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Formats credential display names using Strategy Pattern
 *
 * Single Responsibility: Handle credential display formatting
 * Open/Closed Principle: Uses strategy pattern - new formats can be added without modifying this class
 *
 * @par Migration Note
 * This class now uses DisplayStrategyFactory internally. The enum-based API is deprecated.
 * Use the string-based formatDisplayName() method instead.
 *
 * @deprecated The DisplayFormat enum and formatFromString() are deprecated.
 *             Use formatDisplayName(credential, identifier) directly.
 */
class CredentialFormatter
{
public:
    /**
     * @deprecated Use formatDisplayName(credential, QString) instead
     */
    enum DisplayFormat {
        Name,         ///< Show only name (issuer or username) - use "name"
        NameUser,     ///< Show name (username) format - use "name_user"
        Full          ///< Show name (username) - code format - use "full"
    };

    /**
     * @brief Formats credential for display using strategy identifier
     *
     * Recommended method: Uses Strategy Pattern for flexible formatting.
     *
     * @param credential Credential to format
     * @param identifier Strategy identifier ("name", "name_user", "full")
     * @return Formatted string
     *
     * @note Thread-safe
     *
     * @par Usage
     * @code
     * QString format = config->displayFormat(); // "name_user"
     * QString display = CredentialFormatter::formatDisplayName(credential, format);
     * @endcode
     */
    static QString formatDisplayName(const OathCredential &credential, const QString &identifier);

    /**
     * @brief Formats CredentialInfo for display using strategy identifier
     *
     * Overload for D-Bus CredentialInfo type.
     *
     * @param credential Credential to format (from D-Bus)
     * @param identifier Strategy identifier ("name", "name_user", "full")
     * @return Formatted string
     */
    static QString formatDisplayName(const CredentialInfo &credential, const QString &identifier);

    /**
     * @brief Formats CredentialInfo for display using enum format
     *
     * Overload for D-Bus CredentialInfo type.
     *
     * @param credential Credential to format (from D-Bus)
     * @param format Display format enum
     * @return Formatted string
     */
    static QString formatDisplayName(const CredentialInfo &credential, DisplayFormat format);

    /**
     * @brief Formats credential for display using enum format
     *
     * @deprecated Use formatDisplayName(credential, QString) instead.
     *             This method is maintained for backward compatibility only.
     *
     * @param credential Credential to format
     * @param format Display format enum
     * @return Formatted string
     */
    static QString formatDisplayName(const OathCredential &credential, DisplayFormat format);

    /**
     * @brief Converts format string to enum
     *
     * @deprecated This method is no longer needed. Use string identifiers directly.
     *
     * @param formatString Format string ("name", "name_user", "full")
     * @return Display format enum
     */
    static DisplayFormat formatFromString(const QString &formatString);

    /**
     * @brief Gets default format identifier
     * @return "name_user"
     */
    static QString defaultFormat();
};

} // namespace YubiKey
} // namespace KRunner
