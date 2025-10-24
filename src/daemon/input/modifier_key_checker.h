/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Utility class for checking keyboard modifier key states
 *
 * Single Responsibility: Detect and wait for modifier key release
 *
 * @par Checked Modifiers
 * - Shift (left and right)
 * - Control (left and right)
 * - Alt (left and right)
 * - AltGr (GroupSwitchModifier)
 *
 * @par Design Pattern
 * Static utility class using X11 XQueryKeymap for modifier detection.
 * Works on X11 and XWayland (requires $DISPLAY environment variable).
 *
 * @par Thread Safety
 * All methods must be called from the main/UI thread.
 *
 * @par Usage Example
 * @code
 * // Check if any modifiers are pressed
 * if (ModifierKeyChecker::hasModifiersPressed()) {
 *     qDebug() << "Pressed modifiers:" << ModifierKeyChecker::getPressedModifiers();
 *
 *     // Wait up to 500ms for release
 *     if (ModifierKeyChecker::waitForModifierRelease(500, 50)) {
 *         qDebug() << "Modifiers released!";
 *     } else {
 *         qDebug() << "Timeout - modifiers still pressed";
 *     }
 * }
 * @endcode
 */
class ModifierKeyChecker
{
public:
    /**
     * @brief Checks if any monitored modifier keys are currently pressed
     *
     * Checks: Shift, Control, Alt, AltGr (GroupSwitchModifier)
     * Does NOT check: Meta/Windows, Keypad
     *
     * @return true if at least one modifier is pressed
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    static bool hasModifiersPressed();

    /**
     * @brief Waits for all modifier keys to be released
     *
     * Polls keyboard state at regular intervals until either:
     * - All modifiers are released (returns true)
     * - Timeout expires (returns false)
     *
     * @param timeoutMs Maximum time to wait in milliseconds (default: 500ms)
     * @param pollIntervalMs How often to check keyboard state in milliseconds (default: 50ms)
     *
     * @return true if modifiers were released within timeout, false if timeout expired
     *
     * @note This is a blocking call that processes events while waiting.
     *       The UI remains responsive during the wait.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    static bool waitForModifierRelease(int timeoutMs = 500, int pollIntervalMs = 50);

    /**
     * @brief Gets human-readable names of currently pressed modifiers
     *
     * Returns localized names for debugging and user notifications.
     *
     * @return List of modifier names (e.g., ["Shift", "Control"])
     *         Empty list if no modifiers are pressed
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    static QStringList getPressedModifiers();

private:
    // Static utility class - no instances
    ModifierKeyChecker() = delete;
    ~ModifierKeyChecker() = delete;
    ModifierKeyChecker(const ModifierKeyChecker&) = delete;
    ModifierKeyChecker& operator=(const ModifierKeyChecker&) = delete;
};

} // namespace YubiKey
} // namespace KRunner
