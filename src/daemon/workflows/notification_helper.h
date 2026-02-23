/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>

namespace YubiKeyOath {
namespace Shared {
class ConfigurationProvider;
}

namespace Daemon {
using Shared::ConfigurationProvider;

/**
 * @brief Helper utilities for notification timing calculations
 *
 * Provides centralized logic for calculating notification durations
 * and timer progress across different notification types.
 *
 * Single Responsibility: Notification timing calculations
 */
namespace NotificationHelper
{

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
int calculateNotificationDuration(const ConfigurationProvider *config);

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
TimerProgress calculateTimerProgress(const QDateTime &expirationTime,
                                     int totalSeconds);

} // namespace NotificationHelper

} // namespace Daemon
} // namespace YubiKeyOath
