/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_database.h"
#include "transaction_guard.h"
#include "../logging_categories.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QVariant>
#include <QRegularExpression>

namespace YubiKeyOath {
namespace Daemon {

OathDatabase::OathDatabase(QObject *parent)
    : QObject(parent)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Constructor called";
}

OathDatabase::~OathDatabase()
{
    const QString connectionName = m_db.connectionName();
    if (m_db.isOpen()) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Closing database connection";
        m_db.close();
    }

    // IMPORTANT: Release the database reference BEFORE removing the connection
    // Assign a null database to m_db to release the reference to the connection
    qCDebug(OathDatabaseLog) << "OathDatabase: Releasing database reference";
    m_db = QSqlDatabase();  // Release reference by assigning null database

    // Now remove the database connection
    if (!connectionName.isEmpty()) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Removing database connection:" << connectionName;
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool OathDatabase::isValidDeviceId(const QString &deviceId)
{
    // Trim whitespace defensively to handle any formatting inconsistencies
    const QString trimmed = deviceId.trimmed();

    // Device ID must be exactly 16 hexadecimal characters (64-bit hex string)
    // Example: "28b5c0b54ccb10db"
    static const QRegularExpression hexPattern(QStringLiteral("^[0-9a-fA-F]{16}$"));
    const bool isValid = hexPattern.match(trimmed).hasMatch();

    if (!isValid) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Invalid device ID format:"
                                      << "original:'" << deviceId << "'"
                                      << "trimmed:'" << trimmed << "'"
                                      << "original length:" << deviceId.length()
                                      << "trimmed length:" << trimmed.length();
    }

    return isValid;
}

QString OathDatabase::getDatabasePath() const
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataPath + QStringLiteral("/krunner-yubikey/devices.db");
}

bool OathDatabase::ensureDirectoryExists() const
{
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dbDir = dataPath + QStringLiteral("/krunner-yubikey");

    const QDir dir;
    if (!dir.exists(dbDir)) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Creating directory:" << dbDir;
        if (!dir.mkpath(dbDir)) {
            qCWarning(OathDatabaseLog) << "OathDatabase: Failed to create directory:" << dbDir;
            return false;
        }
    }

    return true;
}

bool OathDatabase::initialize()
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Initializing database";

    // Ensure directory exists
    if (!ensureDirectoryExists()) {
        return false;
    }

    // Get database path
    QString const dbPath = getDatabasePath();
    qCDebug(OathDatabaseLog) << "OathDatabase: Database path:" << dbPath;

    // Open SQLite database
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("yubikey_devices"));
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to open database:"
                                      << m_db.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Database opened successfully";

    // Enable foreign key constraints (required for CASCADE DELETE)
    QSqlQuery pragmaQuery(m_db);
    if (!pragmaQuery.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to enable foreign keys:"
                                      << pragmaQuery.lastError().text();
        return false;
    }
    qCDebug(OathDatabaseLog) << "OathDatabase: Foreign key constraints enabled";

    // Create tables
    if (!createTables()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to create tables";
        return false;
    }

    // Migrate schema if needed (add new columns to existing tables)
    if (!checkAndMigrateSchema()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to migrate schema";
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Initialization complete";
    return true;
}

bool OathDatabase::createTables()
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Creating tables if they don't exist";

    QSqlQuery query(m_db);

    // Create devices table
    QString const createDevicesTableSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS devices ("
        "device_id TEXT PRIMARY KEY, "
        "device_name TEXT NOT NULL, "
        "requires_password INTEGER NOT NULL DEFAULT 0, "
        "last_seen TEXT, "
        "created_at TEXT NOT NULL, "
        "firmware_version TEXT, "
        "device_model INTEGER, "
        "serial_number INTEGER, "
        "form_factor INTEGER"
        ")"
    );

    if (!query.exec(createDevicesTableSql)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to create devices table:"
                                      << query.lastError().text();
        return false;
    }

    // Create credentials table (for caching)
    QString const createCredentialsTableSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS credentials ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "device_id TEXT NOT NULL, "
        "credential_name TEXT NOT NULL, "
        "issuer TEXT, "
        "account TEXT, "
        "period INTEGER DEFAULT 30, "
        "algorithm INTEGER DEFAULT 1, "
        "digits INTEGER DEFAULT 6, "
        "type INTEGER DEFAULT 2, "
        "requires_touch INTEGER DEFAULT 0, "
        "FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE, "
        "UNIQUE(device_id, credential_name)"
        ")"
    );

    if (!query.exec(createCredentialsTableSql)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to create credentials table:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Tables created/verified successfully";
    return true;
}

bool OathDatabase::addColumnIfNotExists(const QString &columnName, const QString &columnType)
{
    // Whitelist validation for column names and types (security: prevent SQL injection)
    // Even though this function is only called with hardcoded literals, we validate
    // as a defense-in-depth measure and to enforce secure coding practices.
    static const QSet<QString> allowedColumns = {
        QStringLiteral("firmware_version"),
        QStringLiteral("device_model"),
        QStringLiteral("serial_number"),
        QStringLiteral("form_factor")
    };
    static const QSet<QString> allowedTypes = {
        QStringLiteral("TEXT"),
        QStringLiteral("INTEGER")
    };

    if (!allowedColumns.contains(columnName)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Rejected attempt to add non-whitelisted column:" << columnName;
        return false;
    }

    if (!allowedTypes.contains(columnType)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Rejected attempt to use non-whitelisted column type:" << columnType;
        return false;
    }

    // Get current columns in devices table
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(devices)"))) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to get table info:"
                                      << query.lastError().text();
        return false;
    }

    // Check if column exists
    bool columnExists = false;
    while (query.next()) {
        if (query.value(1).toString() == columnName) {  // Column 1 is 'name'
            columnExists = true;
            break;
        }
    }

    // Column already exists - success
    if (columnExists) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Column already exists:" << columnName;
        return true;
    }

    // Add missing column
    qCDebug(OathDatabaseLog) << "OathDatabase: Adding missing column:" << columnName;

    // Note: Cannot use prepared statements for DDL operations (ALTER TABLE, CREATE TABLE, etc.)
    // SQLite and most databases don't support parameter binding for schema modification.
    // Security is ensured through whitelist validation above.
    const QString alterSql = QStringLiteral("ALTER TABLE devices ADD COLUMN %1 %2")
                            .arg(columnName, columnType);

    QSqlQuery alterQuery(m_db);
    if (!alterQuery.exec(alterSql)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to add column"
                                      << columnName << ":" << alterQuery.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Column added successfully:" << columnName;
    return true;
}

bool OathDatabase::checkAndMigrateSchema()
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Checking and migrating schema if needed";

    // Add columns if missing (delegates to helper)
    if (!addColumnIfNotExists(QStringLiteral("firmware_version"), QStringLiteral("TEXT"))) {
        return false;
    }
    if (!addColumnIfNotExists(QStringLiteral("device_model"), QStringLiteral("INTEGER"))) {
        return false;
    }
    if (!addColumnIfNotExists(QStringLiteral("serial_number"), QStringLiteral("INTEGER"))) {
        return false;
    }
    if (!addColumnIfNotExists(QStringLiteral("form_factor"), QStringLiteral("INTEGER"))) {
        return false;
    }

    // Migrate NULL last_seen values to created_at (for devices added before this feature)
    QSqlQuery updateQuery(m_db);
    if (!updateQuery.exec(QStringLiteral(
        "UPDATE devices SET last_seen = created_at WHERE last_seen IS NULL OR last_seen = ''"
    ))) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to migrate NULL last_seen values:"
                                      << updateQuery.lastError().text();
        return false;
    }

    int const rowsUpdated = updateQuery.numRowsAffected();
    if (rowsUpdated > 0) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Migrated" << rowsUpdated
                                    << "devices with NULL last_seen to use created_at";
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Schema migration complete";
    return true;
}

bool OathDatabase::addDevice(const QString &deviceId, const QString &name, bool requiresPassword)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Adding device:" << deviceId
                                << "name:" << name << "requiresPassword:" << requiresPassword;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot add device - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO devices (device_id, device_name, requires_password, created_at, last_seen) "
        "VALUES (:device_id, :device_name, :requires_password, :created_at, :last_seen)"
    ));

    QString const currentTime = QDateTime::currentDateTime().toString(Qt::ISODate);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":device_name"), name);
    query.bindValue(QStringLiteral(":requires_password"), requiresPassword ? 1 : 0);
    query.bindValue(QStringLiteral(":created_at"), currentTime);
    query.bindValue(QStringLiteral(":last_seen"), currentTime);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to add device:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Device added successfully";
    return true;
}

bool OathDatabase::updateDeviceName(const QString &deviceId, const QString &name)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Updating device name:" << deviceId << "to:" << name;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot update device name - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE devices SET device_name = :name WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to update device name:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Device name updated successfully";
    return true;
}

bool OathDatabase::updateLastSeen(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Updating last seen for device:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot update last seen - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE devices SET last_seen = :last_seen WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":last_seen"), QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to update last seen:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Last seen updated successfully";
    return true;
}

bool OathDatabase::removeDevice(const QString &deviceId)
{
    // Trim whitespace defensively to match validation
    const QString trimmedId = deviceId.trimmed();
    qCDebug(OathDatabaseLog) << "OathDatabase: Removing device:" << trimmedId;

    if (!isValidDeviceId(trimmedId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot remove device - invalid device ID format:" << trimmedId;
        return false;
    }

    // Defensive delete: Clear credentials first (belt + suspenders with CASCADE DELETE)
    if (!clearDeviceCredentials(trimmedId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to clear credentials before device removal";
        // Continue anyway - CASCADE DELETE should handle this
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), trimmedId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to remove device:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Device removed successfully";
    return true;
}

std::optional<OathDatabase::DeviceRecord> OathDatabase::getDevice(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Getting device:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot get device - invalid device ID format:" << deviceId;
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT device_id, device_name, requires_password, last_seen, created_at, "
        "firmware_version, device_model, serial_number, form_factor "
        "FROM devices WHERE device_id = :device_id"
    ));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to query device:"
                                      << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next()) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Device not found:" << deviceId;
        return std::nullopt;
    }

    DeviceRecord record;
    record.deviceId = query.value(0).toString();
    record.deviceName = query.value(1).toString();
    record.requiresPassword = query.value(2).toInt() != 0;

    QString const lastSeenStr = query.value(3).toString();
    if (!lastSeenStr.isEmpty()) {
        record.lastSeen = QDateTime::fromString(lastSeenStr, Qt::ISODate);
    }

    QString const createdAtStr = query.value(4).toString();
    if (!createdAtStr.isEmpty()) {
        record.createdAt = QDateTime::fromString(createdAtStr, Qt::ISODate);
    }

    // Parse firmware version, device model, serial number, form factor
    QString const firmwareVersionStr = query.value(5).toString();
    if (!firmwareVersionStr.isEmpty()) {
        record.firmwareVersion = Version::fromString(firmwareVersionStr);
    }

    record.deviceModel = query.value(6).toUInt();
    record.serialNumber = query.value(7).toUInt();
    record.formFactor = static_cast<quint8>(query.value(8).toUInt());

    qCDebug(OathDatabaseLog) << "OathDatabase: Device found:" << record.deviceName;
    return record;
}

QList<OathDatabase::DeviceRecord> OathDatabase::getAllDevices()
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Getting all devices";

    QList<DeviceRecord> devices;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
        "SELECT device_id, device_name, requires_password, last_seen, created_at, "
        "firmware_version, device_model, serial_number, form_factor FROM devices"
    ))) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to query devices:"
                                      << query.lastError().text();
        return devices;
    }

    while (query.next()) {
        DeviceRecord record;
        record.deviceId = query.value(0).toString();
        record.deviceName = query.value(1).toString();
        record.requiresPassword = query.value(2).toInt() != 0;

        QString const lastSeenStr = query.value(3).toString();
        if (!lastSeenStr.isEmpty()) {
            record.lastSeen = QDateTime::fromString(lastSeenStr, Qt::ISODate);
        }

        QString const createdAtStr = query.value(4).toString();
        if (!createdAtStr.isEmpty()) {
            record.createdAt = QDateTime::fromString(createdAtStr, Qt::ISODate);
        }

        // Parse firmware version, device model, serial number, form factor
        QString const firmwareVersionStr = query.value(5).toString();
        if (!firmwareVersionStr.isEmpty()) {
            record.firmwareVersion = Version::fromString(firmwareVersionStr);
        }

        record.deviceModel = query.value(6).toUInt();
        record.serialNumber = query.value(7).toUInt();
        record.formFactor = static_cast<quint8>(query.value(8).toUInt());

        devices.append(record);
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Found" << devices.size() << "devices";
    return devices;
}

bool OathDatabase::setRequiresPassword(const QString &deviceId, bool requiresPassword)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Setting requires_password for device:"
                                << deviceId << "to:" << requiresPassword;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot set requires_password - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE devices SET requires_password = :requires_password WHERE device_id = :device_id"
    ));
    query.bindValue(QStringLiteral(":requires_password"), requiresPassword ? 1 : 0);
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to update requires_password:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: requires_password updated successfully";
    return true;
}

bool OathDatabase::requiresPassword(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Checking if device requires password:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot check requires_password - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT requires_password FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to query requires_password:"
                                      << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Device not found, returning false";
        return false;
    }

    bool const requiresPass = query.value(0).toInt() != 0;
    qCDebug(OathDatabaseLog) << "OathDatabase: Device requires password:" << requiresPass;
    return requiresPass;
}

bool OathDatabase::hasDevice(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Checking if device exists:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot check device existence - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to check device existence:"
                                      << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        return false;
    }

    bool const exists = query.value(0).toInt() > 0;
    qCDebug(OathDatabaseLog) << "OathDatabase: Device exists:" << exists;
    return exists;
}

int OathDatabase::countDevicesWithNamePrefix(const QString &prefix)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Counting devices with name prefix:" << prefix;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM devices WHERE device_name LIKE :prefix || '%'"));
    query.bindValue(QStringLiteral(":prefix"), prefix);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to count devices with prefix:"
                                      << query.lastError().text();
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    int const count = query.value(0).toInt();
    qCDebug(OathDatabaseLog) << "OathDatabase: Devices with prefix count:" << count;
    return count;
}

bool OathDatabase::updateDeviceInfo(const QString &deviceId,
                                        const Version &firmwareVersion,
                                        YubiKeyModel deviceModel,
                                        quint32 serialNumber,
                                        quint8 formFactor)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Updating device info for:" << deviceId
                                << "firmware:" << firmwareVersion.toString()
                                << "model:" << deviceModel
                                << "serial:" << serialNumber
                                << "formFactor:" << formFactor;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot update device info - invalid device ID format:" << deviceId;
        return false;
    }

    // First, check if values are different from database
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare(QStringLiteral(
        "SELECT firmware_version, device_model, serial_number, form_factor "
        "FROM devices WHERE device_id = :device_id"
    ));
    checkQuery.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!checkQuery.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to check current device info:"
                                      << checkQuery.lastError().text();
        return false;
    }

    if (!checkQuery.next()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Device not found:" << deviceId;
        return false;
    }

    // Check if values differ
    const QString dbFirmware = checkQuery.value(0).toString();
    const quint32 dbModel = checkQuery.value(1).toUInt();
    const quint32 dbSerial = checkQuery.value(2).toUInt();
    const quint8 dbFormFactor = static_cast<quint8>(checkQuery.value(3).toUInt());

    const QString newFirmware = firmwareVersion.toString();

    if (dbFirmware == newFirmware &&
        dbModel == deviceModel &&
        dbSerial == serialNumber &&
        dbFormFactor == formFactor) {
        qCDebug(OathDatabaseLog) << "OathDatabase: Device info unchanged, skipping update";
        return true;  // No update needed
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Device info changed, updating database";

    // Update device info
    QSqlQuery updateQuery(m_db);
    updateQuery.prepare(QStringLiteral(
        "UPDATE devices SET "
        "firmware_version = :firmware_version, "
        "device_model = :device_model, "
        "serial_number = :serial_number, "
        "form_factor = :form_factor "
        "WHERE device_id = :device_id"
    ));

    updateQuery.bindValue(QStringLiteral(":firmware_version"), newFirmware);
    updateQuery.bindValue(QStringLiteral(":device_model"), deviceModel);
    updateQuery.bindValue(QStringLiteral(":serial_number"), serialNumber);
    updateQuery.bindValue(QStringLiteral(":form_factor"), formFactor);
    updateQuery.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!updateQuery.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to update device info:"
                                      << updateQuery.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Device info updated successfully";
    return true;
}

bool OathDatabase::deleteOldCredentials(const QString &deviceId)
{
    // Belt-and-suspenders validation (caller should validate, but double-check)
    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot delete credentials - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery deleteQuery(m_db);
    deleteQuery.prepare(QStringLiteral("DELETE FROM credentials WHERE device_id = :device_id"));
    deleteQuery.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!deleteQuery.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to delete old credentials:"
                                      << deleteQuery.lastError().text();
        return false;
    }
    return true;
}

bool OathDatabase::insertNewCredentials(const QString &deviceId, const QList<OathCredential> &credentials)
{
    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO credentials (device_id, credential_name, issuer, account, period, "
        "algorithm, digits, type, requires_touch) "
        "VALUES (:device_id, :credential_name, :issuer, :account, :period, "
        ":algorithm, :digits, :type, :requires_touch)"
    ));

    for (const auto &cred : credentials) {
        insertQuery.bindValue(QStringLiteral(":device_id"), deviceId);
        insertQuery.bindValue(QStringLiteral(":credential_name"), cred.originalName);
        insertQuery.bindValue(QStringLiteral(":issuer"), cred.issuer);
        insertQuery.bindValue(QStringLiteral(":account"), cred.account);
        insertQuery.bindValue(QStringLiteral(":period"), cred.period);
        insertQuery.bindValue(QStringLiteral(":algorithm"), static_cast<int>(cred.algorithm));
        insertQuery.bindValue(QStringLiteral(":digits"), cred.digits);
        insertQuery.bindValue(QStringLiteral(":type"), static_cast<int>(cred.type));
        insertQuery.bindValue(QStringLiteral(":requires_touch"), cred.requiresTouch ? 1 : 0);

        if (!insertQuery.exec()) {
            qCWarning(OathDatabaseLog) << "OathDatabase: Failed to insert credential:"
                                          << cred.originalName << insertQuery.lastError().text();
            return false;
        }
    }
    return true;
}

bool OathDatabase::saveCredentials(const QString &deviceId, const QList<OathCredential> &credentials)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Saving" << credentials.size()
                                << "credentials for device:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot save credentials - invalid device ID format:" << deviceId;
        return false;
    }

    // RAII transaction guard - auto-rollback on early return or exception
    TransactionGuard guard(m_db);
    if (!guard.isValid()) {
        return false; // Transaction failed to start
    }

    // Delete old credentials
    if (!deleteOldCredentials(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to delete old credentials for device:" << deviceId;
        return false; // Guard auto-rollbacks in destructor
    }

    // Insert new credentials
    if (!insertNewCredentials(deviceId, credentials)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to insert new credentials for device:" << deviceId;
        return false; // Guard auto-rollbacks in destructor
    }

    // Commit transaction
    if (!guard.commit()) {
        return false; // Commit failed, guard already rolled back
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Successfully saved" << credentials.size()
                                << "credentials for device:" << deviceId;
    return true;
}

QList<OathCredential> OathDatabase::getCredentials(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Getting credentials for device:" << deviceId;

    QList<OathCredential> credentials;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot get credentials - invalid device ID format:" << deviceId;
        return credentials;  // Return empty list
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT credential_name, issuer, account, period, algorithm, digits, type, requires_touch "
        "FROM credentials WHERE device_id = :device_id"
    ));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to query credentials:"
                                      << query.lastError().text();
        return credentials;
    }

    while (query.next()) {
        OathCredential cred;
        cred.originalName = query.value(0).toString();
        cred.issuer = query.value(1).toString();
        cred.account = query.value(2).toString();
        cred.period = query.value(3).toInt();
        cred.algorithm = static_cast<OathAlgorithm>(query.value(4).toInt());
        cred.digits = query.value(5).toInt();
        cred.type = static_cast<OathType>(query.value(6).toInt());
        cred.requiresTouch = query.value(7).toInt() != 0;
        cred.isTotp = (cred.type == OathType::TOTP);
        cred.deviceId = deviceId;
        // Note: code and validUntil are not stored in cache

        credentials.append(cred);
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Found" << credentials.size()
                                << "credentials for device:" << deviceId;
    return credentials;
}

bool OathDatabase::clearAllCredentials()
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Clearing all credentials";

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM credentials"))) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to clear credentials:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: All credentials cleared";
    return true;
}

bool OathDatabase::clearDeviceCredentials(const QString &deviceId)
{
    qCDebug(OathDatabaseLog) << "OathDatabase: Clearing credentials for device:" << deviceId;

    if (!isValidDeviceId(deviceId)) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Cannot clear credentials - invalid device ID format:" << deviceId;
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM credentials WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(OathDatabaseLog) << "OathDatabase: Failed to clear device credentials:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(OathDatabaseLog) << "OathDatabase: Credentials cleared for device:" << deviceId;
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
