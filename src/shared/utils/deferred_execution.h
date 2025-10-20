/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QTimer>
#include <utility>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Utility for deferred code execution
 *
 * Provides convenient wrappers for QTimer::singleShot with better
 * type safety and readability. Header-only for zero overhead.
 *
 * @par Use Cases
 * - Defer cleanup to next event loop iteration
 * - Schedule async task execution
 * - Avoid calling code from callbacks
 *
 * @par Thread Safety
 * Must be called from main/UI thread (requires Qt event loop).
 */
namespace DeferredExecution {

/**
 * @brief Execute function on next event loop iteration
 *
 * Defers execution to next Qt event loop cycle (0ms delay).
 * Useful for avoiding re-entrancy issues or cleanup from callbacks.
 *
 * @param func Function/lambda to execute
 *
 * @par Example
 * @code
 * // Defer cleanup to avoid deleting object in its callback
 * DeferredExecution::defer([this]() { cleanup(); });
 * @endcode
 */
template<typename Func>
inline void defer(Func&& func)
{
    QTimer::singleShot(0, std::forward<Func>(func));
}

/**
 * @brief Execute function with receiver context
 *
 * Defers execution with Qt object context. Execution is cancelled
 * if receiver is deleted before timer fires.
 *
 * @param receiver QObject context (execution cancelled if deleted)
 * @param func Function/lambda to execute
 *
 * @par Example
 * @code
 * // Defer with context - safe if 'this' gets deleted
 * DeferredExecution::defer(this, [this]() { cleanup(); });
 * @endcode
 */
template<typename Func>
inline void defer(QObject* receiver, Func&& func)
{
    QTimer::singleShot(0, receiver, std::forward<Func>(func));
}

/**
 * @brief Execute function after specified delay
 *
 * Schedules function execution after delay in milliseconds.
 *
 * @param delayMs Delay in milliseconds before execution
 * @param func Function/lambda to execute
 *
 * @par Example
 * @code
 * // Execute after 1 second
 * DeferredExecution::after(1000, []() {
 *     qDebug() << "Delayed execution";
 * });
 * @endcode
 */
template<typename Func>
inline void after(int delayMs, Func&& func)
{
    QTimer::singleShot(delayMs, std::forward<Func>(func));
}

/**
 * @brief Execute function after delay with receiver context
 *
 * Schedules function with Qt object context. Execution cancelled
 * if receiver deleted before timer fires.
 *
 * @param delayMs Delay in milliseconds
 * @param receiver QObject context
 * @param func Function/lambda to execute
 *
 * @par Example
 * @code
 * // Execute after delay with context safety
 * DeferredExecution::after(1000, this, [this]() {
 *     processTimeout();
 * });
 * @endcode
 */
template<typename Func>
inline void after(int delayMs, QObject* receiver, Func&& func)
{
    QTimer::singleShot(delayMs, receiver, std::forward<Func>(func));
}

} // namespace DeferredExecution

} // namespace YubiKey
} // namespace KRunner
