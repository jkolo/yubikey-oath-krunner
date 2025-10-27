/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <utility>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Result type for unified error handling
 *
 * Provides a type-safe way to return either a value or an error message.
 * Inspired by Rust's Result<T, E> and similar patterns.
 *
 * @tparam T The type of the successful result value
 *
 * Usage:
 * @code
 * Result<QString> generateCode(const QString &name) {
 *     if (name.isEmpty()) {
 *         return Result<QString>::error("Name cannot be empty");
 *     }
 *     QString code = performGeneration(name);
 *     return Result<QString>::success(code);
 * }
 *
 * // Consuming the result
 * auto result = generateCode("myaccount");
 * if (result.isSuccess()) {
 *     qDebug() << "Code:" << result.value();
 * } else {
 *     qWarning() << "Error:" << result.error();
 * }
 * @endcode
 */
template<typename T>
class Result {
public:
    /**
     * @brief Creates a successful result with a value
     * @param value The success value
     * @return Result containing the value
     */
    static Result success(T value) {
        return Result(std::move(value), QString());
    }

    /**
     * @brief Creates an error result with an error message
     * @param errorMessage Description of the error
     * @return Result containing the error
     */
    static Result error(const QString &errorMessage) {
        return Result(T(), errorMessage);
    }

    /**
     * @brief Checks if the result represents success
     * @return true if successful, false if error
     */
    bool isSuccess() const {
        return m_error.isEmpty();
    }

    /**
     * @brief Checks if the result represents an error
     * @return true if error, false if successful
     */
    bool isError() const {
        return !m_error.isEmpty();
    }

    /**
     * @brief Gets the success value
     * @return The value
     * @warning Only call if isSuccess() returns true. Behavior undefined otherwise.
     */
    T value() const {
        Q_ASSERT(isSuccess());
        return m_value;
    }

    /**
     * @brief Gets the success value or a default value if error
     * @param defaultValue Value to return if this is an error
     * @return The success value or defaultValue
     */
    T valueOr(const T &defaultValue) const {
        return isSuccess() ? m_value : defaultValue;
    }

    /**
     * @brief Gets the error message
     * @return Error message, or empty string if successful
     */
    QString error() const {
        return m_error;
    }

    /**
     * @brief Implicit conversion to bool (true = success, false = error)
     * @return true if successful
     *
     * Allows usage like:
     * @code
     * if (auto result = generateCode(...)) {
     *     use(result.value());
     * }
     * @endcode
     */
    explicit operator bool() const {
        return isSuccess();
    }

private:
    Result(T value, QString error)
        : m_value(std::move(value))
        , m_error(std::move(error))
    {
    }

    T m_value;
    QString m_error;
};

/**
 * @brief Specialization of Result for void (no value)
 *
 * Used for operations that don't return a value but can fail.
 *
 * Usage:
 * @code
 * Result<void> initialize() {
 *     if (!checkPreconditions()) {
 *         return Result<void>::error("Preconditions not met");
 *     }
 *     performInitialization();
 *     return Result<void>::success();
 * }
 *
 * // Consuming
 * auto result = initialize();
 * if (!result) {
 *     qWarning() << "Init failed:" << result.error();
 * }
 * @endcode
 */
template<>
class Result<void> {
public:
    /**
     * @brief Creates a successful result (no value)
     * @return Successful result
     */
    static Result success() {
        return Result(QString());
    }

    /**
     * @brief Creates an error result with an error message
     * @param errorMessage Description of the error
     * @return Result containing the error
     */
    static Result error(const QString &errorMessage) {
        return Result(errorMessage);
    }

    /**
     * @brief Checks if the result represents success
     * @return true if successful, false if error
     */
    bool isSuccess() const {
        return m_error.isEmpty();
    }

    /**
     * @brief Checks if the result represents an error
     * @return true if error, false if successful
     */
    bool isError() const {
        return !m_error.isEmpty();
    }

    /**
     * @brief Gets the error message
     * @return Error message, or empty string if successful
     */
    QString error() const {
        return m_error;
    }

    /**
     * @brief Implicit conversion to bool (true = success, false = error)
     * @return true if successful
     */
    explicit operator bool() const {
        return isSuccess();
    }

private:
    explicit Result(QString error)
        : m_error(std::move(error))
    {
    }

    QString m_error;
};

} // namespace Shared
} // namespace YubiKeyOath
