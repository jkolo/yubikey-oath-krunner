/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QVariantMap>
#include <QString>

namespace KRunner {
namespace YubiKey {

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
     * urgency and progress value.
     *
     * @param urgency Urgency level (0=low, 1=normal, 2=critical)
     * @param progressValue Progress bar value (0-100 percent)
     * @return QVariantMap with notification hints
     *
     * @note Thread-safe
     *
     * @par Example
     * @code
     * // Create hints for normal urgency with 75% progress
     * QVariantMap hints = NotificationUtils::createNotificationHints(1, 75);
     * @endcode
     */
    static QVariantMap createNotificationHints(int urgency = 1, int progressValue = 100);
};

} // namespace YubiKey
} // namespace KRunner
