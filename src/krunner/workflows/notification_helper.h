/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>

namespace KRunner {
namespace YubiKey {

class ConfigurationProvider;

/**
 * @brief Helper utilities for notification timing calculations
 *
 * Provides centralized logic for calculating notification durations
 * and timer progress across different notification types.
 *
 * Single Responsibility: Notification timing calculations
 */
class NotificationHelper
{
public:
    /**
     * @brief Calculate total notification duration
     *
     * Combines code validity period with user-configured extra time.
     *
     * @param config Configuration provider for notificationExtraTime
     * @return Total duration in seconds
     *
     * @note Thread-safe
     */
    static int calculateNotificationDuration(const ConfigurationProvider *config);

    /**
     * @brief Progress information for countdown timers
     */
    struct TimerProgress {
        int remainingSeconds;  ///< Seconds until expiration
        int totalSeconds;      ///< Total countdown duration
        int progressPercent;   ///< Progress percentage (0-100)
        bool expired;          ///< Whether timer has expired
    };

    /**
     * @brief Calculate timer progress for countdown notifications
     *
     * @param expirationTime When the timer expires
     * @param totalSeconds Total duration of countdown
     * @return Progress information struct
     *
     * @note Thread-safe
     */
    static TimerProgress calculateTimerProgress(const QDateTime &expirationTime,
                                                 int totalSeconds);
};

} // namespace YubiKey
} // namespace KRunner
