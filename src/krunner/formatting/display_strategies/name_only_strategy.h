/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_display_strategy.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Displays only name (issuer or username)
 *
 * Format: Shows issuer if available, otherwise username.
 * Example: "Google" or "user@example.com"
 *
 * @par Usage
 * Minimal display suitable for compact UIs or when space is limited.
 */
class NameOnlyStrategy : public IDisplayStrategy
{
public:
    /**
     * @brief Formats credential showing only name
     *
     * @param credential Credential to format
     * @return Issuer if present, otherwise username
     */
    QString format(const OathCredential &credential) const override;

    /**
     * @brief Gets strategy identifier
     * @return "name"
     */
    QString identifier() const override;
};

} // namespace YubiKey
} // namespace KRunner
