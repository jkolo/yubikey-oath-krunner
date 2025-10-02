/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <KRunner/QueryMatch>
#include <KRunner/Action>
#include "../../shared/dbus/yubikey_dbus_types.h"

namespace KRunner {
namespace YubiKey {

// Forward declarations
class ConfigurationProvider;
class YubiKeyDBusClient;

/**
 * @brief Builds KRunner QueryMatch objects from credentials
 *
 * Single Responsibility: Construct and configure QueryMatch objects
 * Open/Closed: Easy to extend with new match types
 *
 * @par Match Types
 * - Credential matches: Searchable TOTP/HOTP credentials with copy/type actions
 * - Password error matches: Special match that opens settings when authentication fails
 *
 * @par Relevance Scoring
 * Uses fuzzy matching algorithm to calculate relevance scores (0.0-1.0):
 * - Exact name match: 1.0
 * - Starts with query: 0.9
 * - Contains query: 0.7
 * - Partial match: 0.5-0.6
 *
 * @par Display Format
 * Respects ConfigurationProvider settings for credential display format.
 *
 * @par Usage Example
 * @code
 * MatchBuilder builder(runner, config, actions);
 *
 * // Build match for credential
 * OathCredential cred;
 * cred.name = "Google:user@example.com";
 * cred.deviceId = "ABC123";
 * cred.type = OathCredential::TOTP;
 *
 * KRunner::QueryMatch match = builder.buildCredentialMatch(cred, "google", credProvider);
 * // Creates match with:
 * // - Text: "Google: user@example.com"
 * // - Icon: YubiKey icon
 * // - Actions: Copy, Type
 * // - Relevance: 0.9 (starts with query)
 *
 * // Build error match for authentication failure
 * KRunner::QueryMatch errorMatch = builder.buildPasswordErrorMatch(
 *     "Authentication required for device ABC123");
 * // Creates match that opens settings when activated
 * @endcode
 */
class MatchBuilder
{
public:
    /**
     * @brief Constructs match builder
     *
     * @param runner Parent KRunner for match creation (required by KRunner API)
     * @param config Configuration provider for display format settings
     * @param actions Available actions (copy/type) to attach to matches
     */
    explicit MatchBuilder(KRunner::AbstractRunner *runner,
                         const ConfigurationProvider *config,
                         const KRunner::Actions &actions);

    /**
     * @brief Builds KRunner match for a TOTP/HOTP credential
     *
     * Creates searchable match with:
     * - Display text formatted per config (issuer:account or account@issuer)
     * - YubiKey icon (:/icons/yubikey.svg)
     * - Relevance score based on query match quality
     * - Copy and Type actions attached
     * - Device ID stored in match data
     *
     * @param credential Credential to create match from
     * @param query User's search query for relevance calculation
     * @param dbusClient D-Bus client for code generation
     *
     * @return Configured QueryMatch ready to display in KRunner.
     *         Can be activated to copy code or execute type/copy actions.
     *
     * @note The dbusClient pointer is stored in match data and must
     *       remain valid for the lifetime of the match.
     */
    KRunner::QueryMatch buildCredentialMatch(const CredentialInfo &credential,
                                            const QString &query,
                                            YubiKeyDBusClient *dbusClient);

    /**
     * @brief Builds special match for authentication errors
     *
     * Creates error match that:
     * - Displays device name and short ID to user
     * - Shows YubiKey icon
     * - Opens password dialog when activated
     * - Has high relevance (1.0) to show prominently
     *
     * @param device Device information including name and ID
     *
     * @return QueryMatch that opens password dialog when user selects it.
     *         Useful for guiding users to enter passwords for locked devices.
     */
    KRunner::QueryMatch buildPasswordErrorMatch(const DeviceInfo &device);

protected:
    /**
     * @brief Calculates relevance score for a match
     * @param credential Credential being matched
     * @param query Search query
     * @return Relevance score (0.0 - 1.0)
     *
     * @note Made protected for unit testing access
     */
    qreal calculateRelevance(const CredentialInfo &credential, const QString &query) const;

private:

    KRunner::AbstractRunner *m_runner;
    const ConfigurationProvider *m_config;
    const KRunner::Actions &m_actions;
};

} // namespace YubiKey
} // namespace KRunner
