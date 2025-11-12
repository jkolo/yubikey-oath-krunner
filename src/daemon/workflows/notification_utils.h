/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QVariantMap>
#include <QString>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Notification urgency levels (freedesktop.org specification)
 *
 * Defines standard urgency levels for D-Bus notifications.
 * Critical notifications bypass "Do Not Disturb" mode in KDE Plasma.
 *
 * @see https://specifications.freedesktop.org/notification-spec/latest/ar01s09.html
 *
 * @par Usage Example
 * @code
 * // Touch request notification (Critical - bypasses DND, user must interact physically)
 * QVariantMap hints = NotificationUtils::createNotificationHints(
 *     NotificationUrgency::Critical,  // Bypasses "Do Not Disturb"
 *     100,                             // Progress bar at 100%
 *     "yubikey-5c-nfc"                // Device-specific icon
 * );
 * @endcode
 */
namespace NotificationUrgency {
    constexpr uchar Low = 0;      ///< Non-critical information (e.g., "Joe Bob signed on")
    constexpr uchar Normal = 1;   ///< Standard notifications (e.g., "You have new mail")
    constexpr uchar Critical = 2; ///< Important notifications requiring immediate attention (bypasses DND)
}

/**
 * @brief Utility functions for notification formatting
 *
 * Provides reusable helpers for creating notification hints and formatting.
 * Reduces code duplication across notification management code.
 */
class NotificationUtils
{
public:
    /**
     * @brief Create notification hints map
     *
     * Creates standardized hints map for D-Bus notifications with
     * urgency, progress value, and optional icon.
     *
     * @param urgency Urgency level (use NotificationUrgency constants)
     * @param progressValue Progress bar value (0-100 percent)
     * @param iconName Optional icon theme name for image-path hint (e.g., "yubikey-5c-nfc")
     * @return QVariantMap with notification hints
     *
     * @note Thread-safe
     * @note Urgency type is preserved as uchar (byte) for correct D-Bus type signature
     *
     * @par Example
     * @code
     * // Create critical notification with device icon
     * QVariantMap hints = NotificationUtils::createNotificationHints(
     *     NotificationUrgency::Critical,
     *     100,
     *     "yubikey-5c-nfc"
     * );
     * @endcode
     */
    static QVariantMap createNotificationHints(uchar urgency = NotificationUrgency::Normal,
                                                int progressValue = 100,
                                                const QString& iconName = QString());
};

} // namespace Daemon
} // namespace YubiKeyOath
