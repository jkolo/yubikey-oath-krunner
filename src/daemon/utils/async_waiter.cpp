/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "async_waiter.h"
#include <QCoreApplication>

namespace YubiKeyOath {
namespace Daemon {

AsyncWaiter::WaitResult AsyncWaiter::waitFor(
    const std::function<bool()> &condition,
    int timeoutMs,
    int progressIntervalMs,
    const std::function<void(int)> &onProgress)
{
    WaitResult result{};
    result.success = false;
    result.elapsedMs = 0;
    result.timedOut = false;

    // Check condition immediately
    if (condition()) {
        result.success = true;
        return result;
    }

    // Setup event loop and timeout timer
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    // Setup progress timer if callback provided
    QTimer progressTimer;
    if (onProgress && progressIntervalMs > 0) {
        QObject::connect(&progressTimer, &QTimer::timeout, [&result, progressIntervalMs, onProgress]() {
            result.elapsedMs += progressIntervalMs;
            onProgress(result.elapsedMs);
        });
        progressTimer.start(progressIntervalMs);
    }

    // Wait loop with proper event processing
    while (!condition() && timeoutTimer.isActive()) {
        loop.processEvents(QEventLoop::WaitForMoreEvents, 100);
    }

    // Cleanup timers
    timeoutTimer.stop();
    if (onProgress && progressIntervalMs > 0) {
        progressTimer.stop();
    }

    // Determine result
    result.success = condition();
    result.timedOut = !result.success && !timeoutTimer.isActive();

    // Update elapsed time if not using progress callback
    if (!onProgress || progressIntervalMs <= 0) {
        result.elapsedMs = timeoutMs - timeoutTimer.remainingTime();
    }

    return result;
}

} // namespace Daemon
} // namespace YubiKeyOath
