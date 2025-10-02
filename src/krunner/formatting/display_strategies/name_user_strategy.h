/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_display_strategy.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Displays name with username in parentheses
 *
 * Format: "Issuer (username)" or just issuer/username if one is missing.
 * Example: "Google (user@example.com)"
 *
 * @par Usage
 * Default format providing good balance between detail and readability.
 */
class NameUserStrategy : public IDisplayStrategy
{
public:
    /**
     * @brief Formats credential with issuer and username
     *
     * @param credential Credential to format
     * @return "Issuer (username)" format, or single value if one missing
     */
    QString format(const OathCredential &credential) const override;

    /**
     * @brief Gets strategy identifier
     * @return "name_user"
     */
    QString identifier() const override;
};

} // namespace YubiKey
} // namespace KRunner
