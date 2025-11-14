/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/storage/yubikey_database.h"
#include "types/oath_credential.h"
#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <optional>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of YubiKeyDatabase for testing
 *
 * In-memory implementation without actual SQL database.
 * Inherits from YubiKeyDatabase to be compatible with services.
 */
class MockYubiKeyDatabase : public YubiKeyDatabase
{
    Q_OBJECT

public:
    explicit MockYubiKeyDatabase(QObject *parent = nullptr)
        : YubiKeyDatabase(parent)
        , m_testDbPath(QDir::temp().filePath(
              QStringLiteral("test_yubikey_") +
              QString::number(QDateTime::currentMSecsSinceEpoch()) +
              QStringLiteral(".db")))
    {
        // Use unique database file for each test instance
        // Database will be cleaned up in destructor
        initialize();
    }

    ~MockYubiKeyDatabase() override
    {
        // Clean up test database file
        // Base class destructor will close the database
        QFile::remove(m_testDbPath);
    }

    /**
     * @brief Override to return test database path
     */
    QString getDatabasePath() const override
    {
        return m_testDbPath;
    }

    // ========== Test Helper Methods ==========
    // All YubiKeyDatabase methods are inherited and use SQLite database

    /**
     * @brief Clears all stored data (for test isolation)
     */
    void reset()
    {
        // Clear all data and reinitialize
        clearAllCredentials();
        // Note: No way to clear devices without removing database file
        // Tests should use unique device IDs per test case
        initialize();
    }

    /**
     * @brief Gets number of stored devices
     */
    int deviceCount()
    {
        return getAllDevices().size();
    }

    /**
     * @brief Gets number of credentials for device
     */
    int credentialCount(const QString &deviceId)
    {
        return getCredentials(deviceId).size();
    }

    /**
     * @brief Adds or updates a single credential (test helper)
     * @param credential Credential to add/update
     * @return true if successful
     *
     * This is a test helper - real database uses saveCredentials() for bulk updates.
     * Adds credential to existing list or updates if name matches.
     */
    bool addOrUpdateCredential(const Shared::OathCredential &credential)
    {
        // Get existing credentials for this device
        auto credentials = getCredentials(credential.deviceId);

        // Check if credential with same name exists
        bool found = false;
        for (auto &cred : credentials) {
            if (cred.originalName == credential.originalName) {
                cred = credential;  // Update existing
                found = true;
                break;
            }
        }

        // If not found, add new credential
        if (!found) {
            credentials.append(credential);
        }

        // Save back to database
        return saveCredentials(credential.deviceId, credentials);
    }

private:
    QString m_testDbPath;  ///< Unique database file path for this test instance
};

} // namespace Daemon
} // namespace YubiKeyOath
