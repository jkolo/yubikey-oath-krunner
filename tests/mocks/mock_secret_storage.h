/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/storage/secret_storage.h"
#include <QObject>
#include <QString>
#include <QMap>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of SecretStorage for testing
 *
 * Provides in-memory password storage without requiring KWallet.
 * Inherits from SecretStorage to be compatible with services.
 *
 * Usage:
 * @code
 * MockSecretStorage storage;
 * storage.savePassword("password123", "device1");
 * auto password = storage.loadPasswordSync("device1");
 * QCOMPARE(password.toString(), "password123");
 * @endcode
 */
class MockSecretStorage : public SecretStorage {
    Q_OBJECT

public:
    explicit MockSecretStorage(QObject *parent = nullptr)
        : SecretStorage(parent)
        , m_savePasswordResult(true)
        , m_removePasswordResult(true)
    {}

    // ========== SecretStorage Interface ==========

    /**
     * @brief Loads password from in-memory storage
     * @param deviceId Device identifier
     * @return SecureString with password (empty if not found)
     */
    SecureMemory::SecureString loadPasswordSync(const QString &deviceId) override {
        if (m_passwords.contains(deviceId)) {
            return SecureMemory::SecureString(m_passwords.value(deviceId));
        }
        return SecureMemory::SecureString();
    }

    /**
     * @brief Saves password to in-memory storage
     * @param password Password to save
     * @param deviceId Device identifier
     * @return Configured result (default: true)
     */
    bool savePassword(const QString &password, const QString &deviceId) override {
        if (m_savePasswordResult) {
            m_passwords[deviceId] = password;
            m_savePasswordCalls[deviceId]++;
        }
        return m_savePasswordResult;
    }

    /**
     * @brief Removes password from in-memory storage
     * @param deviceId Device identifier
     * @return Configured result (default: true)
     */
    bool removePassword(const QString &deviceId) override {
        if (m_removePasswordResult && m_passwords.contains(deviceId)) {
            m_passwords.remove(deviceId);
            m_removePasswordCalls[deviceId]++;
            return true;
        }
        return m_removePasswordResult;
    }

    /**
     * @brief Loads portal restore token
     * @return Token or empty string
     */
    QString loadRestoreToken() {
        return m_restoreToken;
    }

    /**
     * @brief Saves portal restore token
     * @param token Token to save
     * @return true if successful
     */
    bool saveRestoreToken(const QString &token) {
        m_restoreToken = token;
        return true;
    }

    /**
     * @brief Removes portal restore token
     * @return true if successful
     */
    bool removeRestoreToken() {
        m_restoreToken.clear();
        return true;
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets return value for savePassword()
     */
    void setSavePasswordResult(bool result) {
        m_savePasswordResult = result;
    }

    /**
     * @brief Sets return value for removePassword()
     */
    void setRemovePasswordResult(bool result) {
        m_removePasswordResult = result;
    }

    /**
     * @brief Checks if password was saved for device
     */
    bool wasPasswordSaved(const QString &deviceId) const {
        return m_passwords.contains(deviceId);
    }

    /**
     * @brief Gets number of times savePassword() was called for device
     */
    int savePasswordCallCount(const QString &deviceId) const {
        return m_savePasswordCalls.value(deviceId, 0);
    }

    /**
     * @brief Gets number of times removePassword() was called for device
     */
    int removePasswordCallCount(const QString &deviceId) const {
        return m_removePasswordCalls.value(deviceId, 0);
    }

    /**
     * @brief Directly sets password (for test setup)
     */
    void setPassword(const QString &deviceId, const QString &password) {
        m_passwords[deviceId] = password;
    }

    /**
     * @brief Checks if device has password stored
     */
    bool hasPassword(const QString &deviceId) const {
        return m_passwords.contains(deviceId);
    }

    /**
     * @brief Gets stored password (for verification)
     * WARNING: Returns raw password - use only in tests!
     */
    QString getStoredPassword(const QString &deviceId) const {
        return m_passwords.value(deviceId);
    }

    /**
     * @brief Clears all stored passwords
     */
    void clear() {
        m_passwords.clear();
        m_savePasswordCalls.clear();
        m_removePasswordCalls.clear();
        m_restoreToken.clear();
        m_savePasswordResult = true;
        m_removePasswordResult = true;
    }

    /**
     * @brief Gets number of stored passwords
     */
    int passwordCount() const {
        return m_passwords.size();
    }

private:
    QMap<QString, QString> m_passwords;
    QMap<QString, int> m_savePasswordCalls;
    QMap<QString, int> m_removePasswordCalls;
    QString m_restoreToken;
    bool m_savePasswordResult;
    bool m_removePasswordResult;
};

} // namespace Daemon
} // namespace YubiKeyOath
