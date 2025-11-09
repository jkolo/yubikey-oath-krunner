/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QString>

namespace YubiKeyOath {
namespace Config {

/**
 * @brief Formatter for relative time strings
 *
 * Converts QDateTime to human-readable relative time strings
 * (e.g., "2 minutes ago", "yesterday", "3 weeks ago").
 *
 * All methods are static - this is a stateless utility class.
 * Extracted from DeviceDelegate to follow Single Responsibility Principle.
 */
class RelativeTimeFormatter
{
public:
    /**
     * @brief Formats QDateTime as relative time string
     * @param dateTime DateTime to format
     * @return Relative time string (e.g., "2 minutes ago", "yesterday")
     *
     * Time ranges:
     * - < 1 minute: "just now"
     * - < 1 hour: "X minutes ago"
     * - < 1 day: "X hours ago"
     * - 1 day: "yesterday"
     * - < 1 week: "X days ago"
     * - < 4 weeks: "X weeks ago"
     * - < 12 months: "X months ago"
     * - >= 12 months: "yyyy-MM-dd" format
     */
    static QString formatRelativeTime(const QDateTime &dateTime);
};

} // namespace Config
} // namespace YubiKeyOath
