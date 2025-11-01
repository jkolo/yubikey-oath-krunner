/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_database.h"
#include "../logging_categories.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QVariant>

namespace YubiKeyOath {
namespace Daemon {

YubiKeyDatabase::YubiKeyDatabase(QObject *parent)
    : QObject(parent)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Constructor called";
}

YubiKeyDatabase::~YubiKeyDatabase()
{
    const QString connectionName = m_db.connectionName();
    if (m_db.isOpen()) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Closing database connection";
        m_db.close();
    }

    // IMPORTANT: Release the database reference BEFORE removing the connection
    // Assign a null database to m_db to release the reference to the connection
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Releasing database reference";
    m_db = QSqlDatabase();  // Release reference by assigning null database

    // Now remove the database connection
    if (!connectionName.isEmpty()) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Removing database connection:" << connectionName;
        QSqlDatabase::removeDatabase(connectionName);
    }
}

QString YubiKeyDatabase::getDatabasePath() const
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataPath + QStringLiteral("/krunner-yubikey/devices.db");
}

bool YubiKeyDatabase::ensureDirectoryExists() const
{
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dbDir = dataPath + QStringLiteral("/krunner-yubikey");

    const QDir dir;
    if (!dir.exists(dbDir)) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Creating directory:" << dbDir;
        if (!dir.mkpath(dbDir)) {
            qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to create directory:" << dbDir;
            return false;
        }
    }

    return true;
}

bool YubiKeyDatabase::initialize()
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Initializing database";

    // Ensure directory exists
    if (!ensureDirectoryExists()) {
        return false;
    }

    // Get database path
    QString const dbPath = getDatabasePath();
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Database path:" << dbPath;

    // Open SQLite database
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("yubikey_devices"));
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to open database:"
                                      << m_db.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Database opened successfully";

    // Create tables
    if (!createTables()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to create tables";
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Initialization complete";
    return true;
}

bool YubiKeyDatabase::createTables()
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Creating tables if they don't exist";

    QSqlQuery query(m_db);

    QString const createTableSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS devices ("
        "device_id TEXT PRIMARY KEY, "
        "device_name TEXT NOT NULL, "
        "requires_password INTEGER NOT NULL DEFAULT 0, "
        "last_seen TEXT, "
        "created_at TEXT NOT NULL"
        ")"
    );

    if (!query.exec(createTableSql)) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to create devices table:"
                                      << query.lastError().text();
        return false;
    }

    // Create credential_metadata table
    QString const createMetadataTableSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS credential_metadata ("
        "device_id TEXT NOT NULL, "
        "credential_name TEXT NOT NULL, "
        "period INTEGER NOT NULL DEFAULT 30, "
        "digits INTEGER NOT NULL DEFAULT 6, "
        "algorithm INTEGER NOT NULL DEFAULT 1, "
        "PRIMARY KEY (device_id, credential_name), "
        "FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE"
        ")"
    );

    if (!query.exec(createMetadataTableSql)) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to create credential_metadata table:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Tables created/verified successfully";
    return true;
}

bool YubiKeyDatabase::addDevice(const QString &deviceId, const QString &name, bool requiresPassword)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Adding device:" << deviceId
                                << "name:" << name << "requiresPassword:" << requiresPassword;

    if (deviceId.isEmpty()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Cannot add device with empty ID";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO devices (device_id, device_name, requires_password, created_at) "
        "VALUES (:device_id, :device_name, :requires_password, :created_at)"
    ));

    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":device_name"), name);
    query.bindValue(QStringLiteral(":requires_password"), requiresPassword ? 1 : 0);
    query.bindValue(QStringLiteral(":created_at"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to add device:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device added successfully";
    return true;
}

bool YubiKeyDatabase::updateDeviceName(const QString &deviceId, const QString &name)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Updating device name:" << deviceId << "to:" << name;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE devices SET device_name = :name WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to update device name:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device name updated successfully";
    return true;
}

bool YubiKeyDatabase::updateLastSeen(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Updating last seen for device:" << deviceId;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE devices SET last_seen = :last_seen WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":last_seen"), QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to update last seen:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Last seen updated successfully";
    return true;
}

bool YubiKeyDatabase::removeDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Removing device:" << deviceId;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to remove device:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device removed successfully";
    return true;
}

std::optional<YubiKeyDatabase::DeviceRecord> YubiKeyDatabase::getDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Getting device:" << deviceId;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT device_id, device_name, requires_password, last_seen, created_at "
        "FROM devices WHERE device_id = :device_id"
    ));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to query device:"
                                      << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next()) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device not found:" << deviceId;
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

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device found:" << record.deviceName;
    return record;
}

QList<YubiKeyDatabase::DeviceRecord> YubiKeyDatabase::getAllDevices()
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Getting all devices";

    QList<DeviceRecord> devices;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
        "SELECT device_id, device_name, requires_password, last_seen, created_at FROM devices"
    ))) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to query devices:"
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

        devices.append(record);
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Found" << devices.size() << "devices";
    return devices;
}

bool YubiKeyDatabase::setRequiresPassword(const QString &deviceId, bool requiresPassword)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Setting requires_password for device:"
                                << deviceId << "to:" << requiresPassword;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE devices SET requires_password = :requires_password WHERE device_id = :device_id"
    ));
    query.bindValue(QStringLiteral(":requires_password"), requiresPassword ? 1 : 0);
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to update requires_password:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: requires_password updated successfully";
    return true;
}

bool YubiKeyDatabase::requiresPassword(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Checking if device requires password:" << deviceId;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT requires_password FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to query requires_password:"
                                      << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device not found, returning false";
        return false;
    }

    bool const requiresPass = query.value(0).toInt() != 0;
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device requires password:" << requiresPass;
    return requiresPass;
}

bool YubiKeyDatabase::hasDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Checking if device exists:" << deviceId;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM devices WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to check device existence:"
                                      << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        return false;
    }

    bool const exists = query.value(0).toInt() > 0;
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Device exists:" << exists;
    return exists;
}

// Credential metadata management

bool YubiKeyDatabase::saveCredentialMetadata(const QString &deviceId,
                                             const QString &credentialName,
                                             int period,
                                             int digits,
                                             int algorithm)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Saving credential metadata for device:" << deviceId
                                << "credential:" << credentialName
                                << "period:" << period << "digits:" << digits << "algorithm:" << algorithm;

    if (deviceId.isEmpty() || credentialName.isEmpty()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Cannot save metadata with empty device_id or credential_name";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO credential_metadata "
        "(device_id, credential_name, period, digits, algorithm) "
        "VALUES (:device_id, :credential_name, :period, :digits, :algorithm)"
    ));

    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":credential_name"), credentialName);
    query.bindValue(QStringLiteral(":period"), period);
    query.bindValue(QStringLiteral(":digits"), digits);
    query.bindValue(QStringLiteral(":algorithm"), algorithm);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to save credential metadata:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Credential metadata saved successfully";
    return true;
}

std::optional<YubiKeyDatabase::CredentialMetadata>
YubiKeyDatabase::getCredentialMetadata(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Getting credential metadata for device:" << deviceId
                                << "credential:" << credentialName;

    if (deviceId.isEmpty() || credentialName.isEmpty()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Cannot get metadata with empty device_id or credential_name";
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT device_id, credential_name, period, digits, algorithm "
        "FROM credential_metadata "
        "WHERE device_id = :device_id AND credential_name = :credential_name"
    ));

    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":credential_name"), credentialName);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to query credential metadata:"
                                      << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next()) {
        qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Credential metadata not found";
        return std::nullopt;
    }

    CredentialMetadata metadata;
    metadata.deviceId = query.value(0).toString();
    metadata.credentialName = query.value(1).toString();
    metadata.period = query.value(2).toInt();
    metadata.digits = query.value(3).toInt();
    metadata.algorithm = query.value(4).toInt();

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Found credential metadata"
                                << "period:" << metadata.period
                                << "digits:" << metadata.digits
                                << "algorithm:" << metadata.algorithm;

    return metadata;
}

bool YubiKeyDatabase::deleteCredentialMetadata(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Deleting credential metadata for device:" << deviceId
                                << "credential:" << credentialName;

    if (deviceId.isEmpty() || credentialName.isEmpty()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Cannot delete metadata with empty device_id or credential_name";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM credential_metadata "
        "WHERE device_id = :device_id AND credential_name = :credential_name"
    ));

    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":credential_name"), credentialName);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to delete credential metadata:"
                                      << query.lastError().text();
        return false;
    }

    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Credential metadata deleted successfully";
    return true;
}

bool YubiKeyDatabase::deleteAllCredentialMetadataForDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Deleting all credential metadata for device:" << deviceId;

    if (deviceId.isEmpty()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Cannot delete metadata with empty device_id";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM credential_metadata WHERE device_id = :device_id"
    ));

    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        qCWarning(YubiKeyDatabaseLog) << "YubiKeyDatabase: Failed to delete all credential metadata:"
                                      << query.lastError().text();
        return false;
    }

    int const deletedCount = query.numRowsAffected();
    qCDebug(YubiKeyDatabaseLog) << "YubiKeyDatabase: Deleted" << deletedCount << "credential metadata entries";
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
