/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../../shared/types/oath_credential.h"
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Interface for credential display formatting strategies
 *
 * Strategy Pattern: Defines family of algorithms for formatting credentials
 * Open/Closed Principle: New formats can be added without modifying existing code
 * Single Responsibility: Each strategy handles one specific formatting style
 *
 * @par Usage Example
 * @code
 * OathCredential cred;
 * cred.issuer = "Google";
 * cred.username = "user@example.com";
 * cred.code = "123456";
 *
 * IDisplayStrategy *strategy = new NameUserStrategy();
 * QString display = strategy->format(cred);
 * // Result: "Google (user@example.com)"
 *
 * delete strategy;
 * @endcode
 */
class IDisplayStrategy
{
public:
    virtual ~IDisplayStrategy() = default;

    /**
     * @brief Formats credential for display
     *
     * @param credential Credential to format
     * @return Formatted display string
     *
     * @note Thread-safe: Can be called from any thread
     */
    virtual QString format(const OathCredential &credential) const = 0;

    /**
     * @brief Gets unique identifier for this strategy
     *
     * Used for configuration persistence and factory lookups.
     *
     * @return Strategy identifier (e.g., "name", "name_user", "full")
     */
    virtual QString identifier() const = 0;
};

} // namespace YubiKey
} // namespace KRunner
