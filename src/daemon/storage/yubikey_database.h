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
#include "types/oath_credential.h"
#include "shared/utils/version.h"
#include "shared/types/yubikey_model.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

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
        Version firmwareVersion;    ///< Firmware version (from Management or OATH SELECT)
        YubiKeyModel deviceModel;   ///< Device model (series, variant, ports, capabilities)
        quint32 serialNumber;       ///< Device serial number (0 if unavailable)
        quint8 formFactor;          ///< Form factor (1=Keychain, 2=Nano, etc., 0 if unavailable)
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

    /**
     * @brief Counts devices with names starting with given prefix
     * @param prefix Name prefix to search for (case-sensitive)
     * @return Number of devices with names starting with prefix
     *
     * Used for generating unique device names with numeric suffixes.
     * Example: countDevicesWithNamePrefix("YubiKey 5C NFC") returns count
     * matching "YubiKey 5C NFC", "YubiKey 5C NFC 2", "YubiKey 5C NFC 3", etc.
     */
    int countDevicesWithNamePrefix(const QString &prefix);

    /**
     * @brief Updates device extended information (firmware, model, serial, form factor)
     * @param deviceId Device ID to update
     * @param firmwareVersion Firmware version from YubiKey
     * @param deviceModel Device model (series, variant, ports)
     * @param serialNumber Device serial number (0 if unavailable)
     * @param formFactor Form factor (0 if unavailable)
     * @return true if successful
     *
     * Updates only if values are different from database.
     * Called when device is connected to synchronize hardware info.
     */
    bool updateDeviceInfo(const QString &deviceId,
                          const Version &firmwareVersion,
                          YubiKeyModel deviceModel,
                          quint32 serialNumber,
                          quint8 formFactor);

    /**
     * @brief Saves/updates credentials for device in cache
     * @param deviceId Device ID
     * @param credentials List of credentials to save
     * @return true if successful
     *
     * Replaces all existing credentials for this device.
     * Used when credential cache is enabled.
     */
    bool saveCredentials(const QString &deviceId, const QList<Shared::OathCredential> &credentials);

    /**
     * @brief Gets cached credentials for device
     * @param deviceId Device ID
     * @return List of cached credentials (empty if none found)
     *
     * Returns credentials from cache, useful when device is offline.
     */
    QList<Shared::OathCredential> getCredentials(const QString &deviceId);

    /**
     * @brief Clears all cached credentials from database
     * @return true if successful
     *
     * Called when credential cache is disabled in settings.
     */
    bool clearAllCredentials();

    /**
     * @brief Clears cached credentials for specific device
     * @param deviceId Device ID
     * @return true if successful
     */
    bool clearDeviceCredentials(const QString &deviceId);

private:
    QSqlDatabase m_db;

    /**
     * @brief Creates database tables if they don't exist
     * @return true if successful
     */
    bool createTables();

    /**
     * @brief Checks and migrates database schema if needed
     * @return true if successful
     *
     * Adds missing columns to existing tables without data loss.
     * Called automatically during initialization.
     */
    bool checkAndMigrateSchema();

    /**
     * @brief Adds column to devices table if it doesn't exist
     * @param columnName Name of column to add
     * @param columnType SQL type of column
     * @return true if column exists or was added successfully
     */
    bool addColumnIfNotExists(const QString &columnName, const QString &columnType);

    /**
     * @brief Gets database file path
     * @return Full path to database file
     *
     * Returns ~/.local/share/krunner-yubikey/devices.db
     * Virtual to allow tests to use temporary database paths.
     */
    virtual QString getDatabasePath() const;

    /**
     * @brief Ensures database directory exists
     * @return true if directory exists or was created successfully
     */
    bool ensureDirectoryExists() const;

    // Helper methods for saveCredentials()
    /**
     * @brief Deletes old credentials for device
     * @param deviceId Device ID
     * @return true if successful
     */
    bool deleteOldCredentials(const QString &deviceId);

    /**
     * @brief Inserts new credentials for device
     * @param deviceId Device ID
     * @param credentials Credentials to insert
     * @return true if all credentials inserted successfully
     */
    bool insertNewCredentials(const QString &deviceId, const QList<Shared::OathCredential> &credentials);

    /**
     * @brief Validates device ID format
     * @param deviceId Device ID to validate
     * @return true if deviceId is valid hex string (16 characters)
     *
     * Device IDs must be 16-character hexadecimal strings from YubiKey OATH.
     * This prevents SQL injection and data corruption.
     *
     * @note Thread-safe: static method with no state
     */
    static bool isValidDeviceId(const QString &deviceId);
};

} // namespace Daemon
} // namespace YubiKeyOath
