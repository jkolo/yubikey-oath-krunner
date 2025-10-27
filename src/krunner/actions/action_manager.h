/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <KRunner/QueryMatch>
#include <QString>

namespace YubiKeyOath {
namespace Runner {

/**
 * @brief Manages action selection and validation for KRunner matches
 *
 * Single Responsibility: Determines which action to execute based on user input
 * and configuration. Separates action selection logic from execution.
 *
 * @par Action Selection Logic
 * 1. Check if KRunner provided a selectedAction (user pressed Shift+Enter)
 * 2. If selectedAction is valid, use it
 * 3. Otherwise, use primary action from configuration (Enter key)
 * 4. Validate that action ID is either "copy" or "type"
 *
 * @par Usage Example
 * @code
 * ActionManager manager;
 * KRunner::QueryMatch match = ...;
 * QString primaryAction = "copy";  // from configuration
 *
 * QString actionToExecute = manager.determineAction(match, primaryAction);
 * // Returns: "copy" or "type" based on user input
 *
 * if (manager.isValidAction(actionToExecute)) {
 *     // Execute the action
 * }
 * @endcode
 */
class ActionManager
{
public:
    /**
     * @brief Determines which action should be executed
     *
     * Checks if user explicitly selected an action (Shift+Enter),
     * otherwise uses the primary action from configuration.
     *
     * @param match KRunner match that was activated
     * @param primaryAction Primary action from configuration ("copy" or "type")
     * @return Action ID to execute ("copy" or "type")
     *
     * @note Always returns a valid action ID (copy or type).
     *       Falls back to primaryAction if selectedAction is invalid.
     */
    QString determineAction(const KRunner::QueryMatch &match,
                           const QString &primaryAction) const;

    /**
     * @brief Validates that action ID is recognized
     *
     * @param actionId Action identifier to validate
     * @return true if actionId is "copy" or "type", false otherwise
     */
    bool isValidAction(const QString &actionId) const;

    /**
     * @brief Gets action name for display/logging
     *
     * @param actionId Action identifier ("copy" or "type")
     * @return Human-readable action name
     */
    QString getActionName(const QString &actionId) const;
};

} // namespace Runner
} // namespace YubiKeyOath
