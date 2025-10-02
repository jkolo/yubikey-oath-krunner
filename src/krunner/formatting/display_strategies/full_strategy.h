/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_display_strategy.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Displays full credential including code or touch status
 *
 * Format:
 * - Non-touch: "Issuer (username) [CODE]" where CODE is the TOTP value
 * - Touch-required: "Issuer (username) [Touch Required]"
 * Example: "Google (user@example.com) [123456]"
 *
 * @par Usage
 * Verbose format showing all credential information including generated code
 * or touch requirement status. Falls back to name_user format if no code provided.
 */
class FullStrategy : public IDisplayStrategy
{
public:
    /**
     * @brief Formats credential with all information
     *
     * @param credential Credential to format
     * @return "Issuer (username)" format (code/touch status added separately)
     */
    QString format(const OathCredential &credential) const override;

    /**
     * @brief Formats credential with code or touch status
     *
     * @param credential Credential to format
     * @param code Generated TOTP/HOTP code (empty if not generated)
     * @param requiresTouch Whether credential requires touch
     * @return "Issuer (username) [code]" or "Issuer (username) [Touch Required]"
     */
    QString formatWithCode(const OathCredential &credential, const QString &code, bool requiresTouch) const;

    /**
     * @brief Gets strategy identifier
     * @return "full"
     */
    QString identifier() const override;
};

} // namespace YubiKey
} // namespace KRunner
