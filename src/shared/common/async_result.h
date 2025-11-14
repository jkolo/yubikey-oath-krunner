/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "result.h"
#include <QFuture>
#include <QString>
#include <QUuid>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Result wrapper for asynchronous operations
 *
 * Provides a tracking handle for long-running async operations.
 * Combines a unique operation ID with a QFuture for the result.
 *
 * @tparam T The type of the successful result value
 *
 * Usage:
 * @code
 * // Service layer - initiating async operation
 * AsyncResult<QString> asyncOp = startCodeGeneration(credentialId);
 * Q_EMIT operationStarted(asyncOp.operationId);
 *
 * // Later, when future completes
 * connect(&asyncOp.futureWatcher, &QFutureWatcher::finished, [asyncOp]() {
 *     Result<QString> result = asyncOp.future.result();
 *     Q_EMIT operationCompleted(asyncOp.operationId, result);
 * });
 *
 * // Client side - tracking operation
 * QString opId = proxy->generateCodeAsync();
 * connect(proxy, &Proxy::codeGenerated, [opId](const QString& id, const QString& code) {
 *     if (id == opId) {
 *         // This is our operation result
 *         displayCode(code);
 *     }
 * });
 * @endcode
 */
template<typename T>
struct AsyncResult {
    /**
     * @brief Unique identifier for tracking this operation
     *
     * Used to match operation initiation with completion signals.
     * Generated using QUuid::createUuid().toString() format.
     */
    QString operationId;

    /**
     * @brief Future representing the async computation
     *
     * Can be monitored via QFutureWatcher or polled directly.
     * Contains Result<T> which may be success or error.
     */
    QFuture<Result<T>> future;

    /**
     * @brief Creates a new async result with auto-generated operation ID
     * @param fut The future to track
     * @return AsyncResult with unique ID and future
     */
    [[nodiscard]] static AsyncResult create(QFuture<Result<T>> fut) {
        return AsyncResult {
            QUuid::createUuid().toString(QUuid::WithoutBraces),
            fut
        };
    }

    /**
     * @brief Creates a new async result with specific operation ID
     * @param opId Custom operation ID (must be unique)
     * @param fut The future to track
     * @return AsyncResult with provided ID and future
     */
    [[nodiscard]] static AsyncResult create(const QString& opId, QFuture<Result<T>> fut) {
        return AsyncResult { opId, fut };
    }

    /**
     * @brief Checks if the future has completed
     * @return true if result is ready, false if still computing
     */
    [[nodiscard]] bool isFinished() const {
        return future.isFinished();
    }

    /**
     * @brief Checks if the future was canceled
     * @return true if operation was canceled
     */
    [[nodiscard]] bool isCanceled() const {
        return future.isCanceled();
    }

    /**
     * @brief Blocks until result is available (or canceled)
     * @warning Only use in test code or worker threads. Never on main/UI thread.
     */
    void waitForFinished() {
        future.waitForFinished();
    }
};

/**
 * @brief Specialization for void results (operation with no return value)
 *
 * Used for operations like "delete credential" or "set password"
 * where only success/failure matters, not a return value.
 */
template<>
struct AsyncResult<void> {
    QString operationId;
    QFuture<Result<void>> future;

    [[nodiscard]] static AsyncResult create(QFuture<Result<void>> fut) {
        return AsyncResult {
            QUuid::createUuid().toString(QUuid::WithoutBraces),
            fut
        };
    }

    [[nodiscard]] static AsyncResult create(const QString& opId, QFuture<Result<void>> fut) {
        return AsyncResult { opId, fut };
    }

    [[nodiscard]] bool isFinished() const {
        return future.isFinished();
    }

    [[nodiscard]] bool isCanceled() const {
        return future.isCanceled();
    }

    void waitForFinished() {
        future.waitForFinished();
    }
};

} // namespace Shared
} // namespace YubiKeyOath
