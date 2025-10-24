/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "notification_helper.h"
#include "config/configuration_provider.h"
#include "../formatting/code_validator.h"
#include "../logging_categories.h"
#include <QDebug>

namespace KRunner {
namespace YubiKey {

int NotificationHelper::calculateNotificationDuration(const ConfigurationProvider *config)
{
    int remainingValidity = CodeValidator::calculateCodeValidity();
    int extraTime = config->notificationExtraTime();
    int totalDuration = remainingValidity + extraTime;

    qCDebug(NotificationOrchestratorLog) << "NotificationHelper: calculateNotificationDuration"
                                         << "remainingValidity:" << remainingValidity
                                         << "extraTime:" << extraTime
                                         << "totalDuration:" << totalDuration;

    return totalDuration;
}

NotificationHelper::TimerProgress NotificationHelper::calculateTimerProgress(
    const QDateTime &expirationTime,
    int totalSeconds)
{
    TimerProgress progress;

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 expiration = expirationTime.toSecsSinceEpoch();

    progress.remainingSeconds = static_cast<int>(expiration - now);
    progress.totalSeconds = totalSeconds;
    progress.expired = progress.remainingSeconds <= 0;

    if (progress.expired) {
        progress.progressPercent = 0;
        progress.remainingSeconds = 0;
    } else {
        progress.progressPercent = (progress.remainingSeconds * 100) / totalSeconds;
    }

    return progress;
}

} // namespace YubiKey
} // namespace KRunner
