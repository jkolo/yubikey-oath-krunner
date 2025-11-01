/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <optional>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manages YubiKey device database using SQLite
 *
 * This class provides persistent storage for YubiKey device information including:
 * - Device ID (unique identifier from YubiKey OATH SELECT response)
 * - Friendly name (user-provided or auto-generated)
 * - Password requirement flag
 * - Last seen timestamp
 *
 * Database location: ~/.local/share/krunner-yubikey/devices.db
 *
 * Single Responsibility: Handle device metadata persistence in SQLite
 */
class YubiKeyDatabase : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Device record structure
     */
    struct DeviceRecord {
        QString deviceId;           ///< Unique device identifier (hex string)
        QString deviceName;         ///< Friendly name
        bool requiresPassword;      ///< Device requires password for OATH access
        QDateTime lastSeen;         ///< Last time device was connected
        QDateTime createdAt;        ///< When device was first added to database
    };

    /**
     * @brief Credential metadata structure
     *
     * Stores per-credential metadata that YubiKey doesn't preserve:
     * - TOTP period (YubiKey doesn't store this!)
     * - Digits count
     * - Algorithm
     *
     * This data is needed for correct TOTP challenge generation.
     */
    struct CredentialMetadata {
        QString deviceId;           ///< Device ID (foreign key to devices table)
        QString credentialName;     ///< Full credential name (issuer:account)
        int period = 30;            ///< TOTP period in seconds (default 30)
        int digits = 6;             ///< Number of digits (6-8)
        int algorithm = 1;          ///< Algorithm: 1=SHA1, 2=SHA256, 3=SHA512
    };

    /**
     * @brief Constructs YubiKeyDatabase instance
     * @param parent Parent QObject
     */
    explicit YubiKeyDatabase(QObject *parent = nullptr);

    /**
     * @brief Destructor - closes database connection
     */
    ~YubiKeyDatabase() override;

    /**
     * @brief Initializes database (creates directory, tables if needed)
     * @return true if initialization successful
     *
     * Creates ~/.local/share/krunner-yubikey/ directory if it doesn't exist
     * Creates database file and tables if they don't exist
     */
    bool initialize();

    /**
     * @brief Adds new device to database
     * @param deviceId Device ID (hex string from YubiKey)
     * @param name Friendly name for device
     * @param requiresPassword Whether device requires password
     * @return true if successful
     *
     * Sets created_at to current timestamp
     */
    bool addDevice(const QString &deviceId, const QString &name, bool requiresPassword);

    /**
     * @brief Updates device friendly name
     * @param deviceId Device ID to update
     * @param name New friendly name
     * @return true if successful
     */
    bool updateDeviceName(const QString &deviceId, const QString &name);

    /**
     * @brief Updates last seen timestamp to current time
     * @param deviceId Device ID to update
     * @return true if successful
     */
    bool updateLastSeen(const QString &deviceId);

    /**
     * @brief Removes device from database
     * @param deviceId Device ID to remove
     * @return true if successful
     */
    bool removeDevice(const QString &deviceId);

    /**
     * @brief Gets device record by ID
     * @param deviceId Device ID to query
     * @return DeviceRecord if found, std::nullopt if not found
     */
    std::optional<DeviceRecord> getDevice(const QString &deviceId);

    /**
     * @brief Gets all devices from database
     * @return List of all device records
     */
    QList<DeviceRecord> getAllDevices();

    /**
     * @brief Sets requires_password flag for device
     * @param deviceId Device ID to update
     * @param requiresPassword New value for requires_password flag
     * @return true if successful
     */
    bool setRequiresPassword(const QString &deviceId, bool requiresPassword);

    /**
     * @brief Checks if device requires password
     * @param deviceId Device ID to check
     * @return true if device requires password, false otherwise
     *
     * Returns false if device not found in database
     */
    bool requiresPassword(const QString &deviceId);

    /**
     * @brief Checks if device exists in database
     * @param deviceId Device ID to check
     * @return true if device exists in database
     */
    bool hasDevice(const QString &deviceId);

    // Credential metadata management
    /**
     * @brief Saves credential metadata (period, digits, algorithm)
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @param period TOTP period in seconds
     * @param digits Number of digits (6-8)
     * @param algorithm Algorithm (1=SHA1, 2=SHA256, 3=SHA512)
     * @return true if successful
     *
     * Inserts or replaces existing metadata for the credential.
     */
    bool saveCredentialMetadata(const QString &deviceId,
                                const QString &credentialName,
                                int period,
                                int digits,
                                int algorithm);

    /**
     * @brief Gets credential metadata by device ID and credential name
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @return CredentialMetadata if found, std::nullopt if not found
     */
    std::optional<CredentialMetadata> getCredentialMetadata(const QString &deviceId,
                                                            const QString &credentialName);

    /**
     * @brief Removes credential metadata from database
     * @param deviceId Device ID
     * @param credentialName Full credential name
     * @return true if successful
     */
    bool deleteCredentialMetadata(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Removes all credential metadata for a device
     * @param deviceId Device ID
     * @return true if successful
     *
     * Called when device is removed from database.
     */
    bool deleteAllCredentialMetadataForDevice(const QString &deviceId);

private:
    QSqlDatabase m_db;

    /**
     * @brief Creates database tables if they don't exist
     * @return true if successful
     */
    bool createTables();

    /**
     * @brief Gets database file path
     * @return Full path to database file
     *
     * Returns ~/.local/share/krunner-yubikey/devices.db
     */
    QString getDatabasePath() const;

    /**
     * @brief Ensures database directory exists
     * @return true if directory exists or was created successfully
     */
    bool ensureDirectoryExists() const;
};

} // namespace Daemon
} // namespace YubiKeyOath
