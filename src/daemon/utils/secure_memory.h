/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QByteArray>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Utilities for secure memory handling of sensitive data
 *
 * Provides secure wiping of memory containing passwords and secrets
 * to prevent exposure via memory dumps, core dumps, or swap files.
 */
class SecureMemory
{
public:
    /**
     * @brief Securely wipes QString contents from memory
     * @param str QString to wipe
     *
     * Overwrites string data with zeros before deallocation.
     * Uses explicit_bzero if available, fallback to volatile memset.
     */
    static void wipeString(QString &str);

    /**
     * @brief Securely wipes QByteArray contents from memory
     * @param data QByteArray to wipe
     *
     * Overwrites byte array with zeros before deallocation.
     */
    static void wipeByteArray(QByteArray &data);

    /**
     * @brief RAII wrapper for QString with automatic secure wiping
     *
     * Use this for passwords and secrets that must be wiped from memory.
     *
     * Example:
     * @code
     * {
     *     SecureString password(loadPasswordFromKWallet());
     *     device->authenticate(password.data());
     *     // password automatically wiped when leaving scope
     * }
     * @endcode
     */
    class SecureString
    {
    public:
        /**
         * @brief Constructs SecureString from QString
         * @param str String to protect (moved)
         */
        explicit SecureString(QString str) : m_data(std::move(str)) {}

        /**
         * @brief Constructs empty SecureString
         */
        SecureString() = default;

        /**
         * @brief Destructor - wipes string from memory
         */
        ~SecureString() {
            SecureMemory::wipeString(m_data);
        }

        // Disable copy to prevent accidental password duplication
        SecureString(const SecureString &) = delete;
        SecureString &operator=(const SecureString &) = delete;

        // Allow move semantics
        SecureString(SecureString &&other) noexcept : m_data(std::move(other.m_data)) {}
        SecureString &operator=(SecureString &&other) noexcept {
            if (this != &other) {
                SecureMemory::wipeString(m_data);
                m_data = std::move(other.m_data);
            }
            return *this;
        }

        /**
         * @brief Access underlying QString
         * @return const reference to QString
         */
        const QString &data() const { return m_data; }

        /**
         * @brief Implicit conversion to QString for API compatibility
         * @return const reference to QString
         *
         * Allows passing SecureString to functions expecting const QString&
         */
        operator const QString&() const { return m_data; }

        /**
         * @brief Check if string is empty
         */
        bool isEmpty() const { return m_data.isEmpty(); }

    private:
        QString m_data;
    };

private:
    SecureMemory() = delete;  // Static utility class
};

} // namespace Daemon
} // namespace YubiKeyOath
