/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <functional>
#include <QEventLoop>
#include <QTimer>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Utility for async condition waiting with event loop processing
 *
 * Provides centralized pattern for waiting on async conditions with:
 * - Proper Qt event loop processing
 * - Configurable timeout
 * - Optional progress callbacks
 * - Early exit on condition met
 *
 * @par Use Cases
 * - Waiting for D-Bus connections
 * - Waiting for device initialization
 * - Waiting for async operations with timeout
 *
 * @par Thread Safety
 * Must be called from main/UI thread (requires Qt event loop).
 */
class AsyncWaiter
{
public:
    /**
     * @brief Result of wait operation
     */
    struct WaitResult {
        bool success;      ///< True if condition met before timeout
        int elapsedMs;     ///< Time elapsed in milliseconds
        bool timedOut;     ///< True if operation timed out

        /// Convenience: check if wait succeeded
        operator bool() const { return success; }
    };

    /**
     * @brief Wait for async condition with timeout and progress logging
     *
     * Processes Qt events in a loop while waiting for condition to become true.
     * Exits early if condition met, otherwise waits until timeout.
     *
     * @param condition Function returning true when wait should end
     * @param timeoutMs Maximum time to wait in milliseconds
     * @param progressIntervalMs How often to call progress callback (default: 500ms)
     * @param onProgress Optional callback called at progress intervals with elapsed time
     * @return WaitResult with success status and elapsed time
     *
     * @note Processes events with WaitForMoreEvents to avoid busy-wait
     * @note Progress callback called from event loop - keep it lightweight
     *
     * @par Example
     * @code
     * auto result = AsyncWaiter::waitFor(
     *     [this]() { return m_connected; },
     *     30000,  // 30 second timeout
     *     500,    // Log every 500ms
     *     [](int elapsed) { qDebug() << "Waiting..." << elapsed << "ms"; }
     * );
     *
     * if (result.success) {
     *     qDebug() << "Connected after" << result.elapsedMs << "ms";
     * } else {
     *     qWarning() << "Timeout after" << result.elapsedMs << "ms";
     * }
     * @endcode
     */
    static WaitResult waitFor(
        std::function<bool()> condition,
        int timeoutMs,
        int progressIntervalMs = 500,
        std::function<void(int)> onProgress = nullptr
    );
};

} // namespace Daemon
} // namespace YubiKeyOath
