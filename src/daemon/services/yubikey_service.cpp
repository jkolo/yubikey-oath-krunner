/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_service.h"
#include "../oath/yubikey_device_manager.h"
#include "types/oath_credential_data.h"
#include "../storage/yubikey_database.h"
#include "../storage/password_storage.h"
#include "../config/daemon_configuration.h"
#include "../actions/yubikey_action_coordinator.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

YubiKeyService::YubiKeyService(QObject *parent)
    : QObject(parent)
    , m_deviceManager(std::make_unique<YubiKeyDeviceManager>(nullptr))
    , m_database(std::make_unique<YubiKeyDatabase>(nullptr))
    , m_passwordStorage(std::make_unique<PasswordStorage>(nullptr))
    , m_config(std::make_unique<DaemonConfiguration>(this))
    , m_actionCoordinator(std::make_unique<YubiKeyActionCoordinator>(m_deviceManager.get(), m_database.get(), m_config.get(), this))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Initializing";

    // Initialize database
    if (!m_database->initialize()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to initialize database";
    }

    // Initialize OATH
    auto initResult = m_deviceManager->initialize();
    if (initResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to initialize OATH:" << initResult.error();
    }

    // Connect internal signals
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceConnected,
            this, &YubiKeyService::onDeviceConnectedInternal);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceDisconnected,
            this, &YubiKeyService::onDeviceDisconnectedInternal);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::credentialCacheFetchedForDevice,
            this, &YubiKeyService::onCredentialCacheFetched);

    // Load passwords for already-connected devices
    const QStringList connectedDevices = m_deviceManager->getConnectedDeviceIds();
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Found" << connectedDevices.size() << "already-connected devices";
    for (const QString &deviceId : connectedDevices) {
        onDeviceConnectedInternal(deviceId);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Initialization complete";
}

YubiKeyService::~YubiKeyService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Destructor";

    if (m_deviceManager) {
        m_deviceManager->cleanup();
    }
}

QList<DeviceInfo> YubiKeyService::listDevices()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: listDevices called";

    QList<DeviceInfo> devices;

    // Get connected devices
    const QStringList connectedDeviceIds = m_deviceManager->getConnectedDeviceIds();

    // Get known devices from database
    const QList<YubiKeyDatabase::DeviceRecord> knownDevices = m_database->getAllDevices();

    // Merge device lists
    QSet<QString> allDeviceIds;
    for (const QString &deviceId : connectedDeviceIds) {
        allDeviceIds.insert(deviceId);
    }
    for (const auto &record : knownDevices) {
        allDeviceIds.insert(record.deviceId);
    }

    // Build device info list
    for (const QString &deviceId : allDeviceIds) {
        DeviceInfo info;
        info.deviceId = deviceId;
        info.isConnected = connectedDeviceIds.contains(deviceId);

        // Get from database
        auto dbRecord = m_database->getDevice(deviceId);
        if (dbRecord.has_value()) {
            info.deviceName = dbRecord->deviceName;
            info.requiresPassword = dbRecord->requiresPassword;
        } else {
            // New device - generate name with full device ID
            info.deviceName = generateDefaultDeviceName(deviceId);
            info.requiresPassword = true;

            // Add to database
            m_database->addDevice(deviceId, info.deviceName, true);
        }

        // Update last seen for connected devices
        if (info.isConnected) {
            m_database->updateLastSeen(deviceId);
        }

        // Check if we have valid password in KWallet
        if (info.requiresPassword) {
            const QString storedPassword = m_passwordStorage->loadPasswordSync(deviceId);
            info.hasValidPassword = !storedPassword.isEmpty();
        } else {
            info.hasValidPassword = true;
        }

        devices.append(info);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Returning" << devices.size() << "devices";
    return devices;
}

QList<OathCredential> YubiKeyService::getCredentials(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: getCredentials for device:" << deviceId;

    QList<OathCredential> credentials;

    if (deviceId.isEmpty()) {
        // Get from all devices
        credentials = m_deviceManager->getCredentials();
    } else {
        // Get from specific device
        auto *device = m_deviceManager->getDevice(deviceId);
        if (device) {
            credentials = device->credentials();
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Returning" << credentials.size() << "credentials";
    return credentials;
}

GenerateCodeResult YubiKeyService::generateCode(const QString &deviceId,
                                                 const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: generateCode for credential:"
                              << credentialName << "on device:" << deviceId;

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << deviceId << "not found";
        return {.code = QString(), .validUntil = 0};
    }

    // Generate code directly on device
    auto result = device->generateCode(credentialName);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to generate code:" << result.error();
        return {.code = QString(), .validUntil = 0};
    }

    const QString code = result.value();

    // Calculate validUntil timestamp (30 seconds from now for TOTP)
    const qint64 validUntil = QDateTime::currentSecsSinceEpoch() + 30;

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Generated code, valid until:" << validUntil;
    return {.code = code, .validUntil = validUntil};
}

bool YubiKeyService::savePassword(const QString &deviceId, const QString &password)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: savePassword for device:" << deviceId;

    // First test the password by attempting authentication
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device not found:" << deviceId;
        return false;
    }

    auto authResult = device->authenticateWithPassword(password);
    if (authResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Password is invalid:" << authResult.error();
        return false;
    }

    // Save password in device for future use
    device->setPassword(password);

    // Save to KWallet
    if (!m_passwordStorage->savePassword(password, deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to save password to KWallet";
        return false;
    }

    // Update database flag
    m_database->setRequiresPassword(deviceId, true);

    // Trigger credential cache refresh with the new password
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Password saved, triggering credential cache refresh";
    device->updateCredentialCacheAsync(password);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Password saved successfully";
    return true;
}

void YubiKeyService::forgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: forgetDevice:" << deviceId;

    // Clear device from memory BEFORE removing from database
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Clearing device from memory";
    clearDeviceFromMemory(deviceId);

    // Remove from database
    m_database->removeDevice(deviceId);

    // Remove password from KWallet
    m_passwordStorage->removePassword(deviceId);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device forgotten";
}

bool YubiKeyService::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: setDeviceName for device:" << deviceId
                              << "new name:" << newName;

    // Validate input
    QString trimmedName = newName.trimmed();
    if (deviceId.isEmpty() || trimmedName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Invalid device ID or name (empty after trim)";
        return false;
    }

    // Validate name length (max 64 chars)
    if (trimmedName.length() > 64) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Name too long (max 64 chars)";
        return false;
    }

    // Check if device exists in database
    if (!m_database->hasDevice(deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device not found in database:" << deviceId;
        return false;
    }

    // Update device name in database
    bool success = m_database->updateDeviceName(deviceId, trimmedName);

    if (success) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device name updated successfully";
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to update device name in database";
    }

    return success;
}

QString YubiKeyService::addCredential(const QString &deviceId,
                                      const QString &name,
                                      const QString &secret,
                                      const QString &type,
                                      const QString &algorithm,
                                      int digits,
                                      int period,
                                      int counter,
                                      bool requireTouch)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: addCredential to device:" << deviceId
                              << "credential:" << name;

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << deviceId << "not found";
        return i18n("Device not found");
    }

    // Build credential data from parameters
    OathCredentialData data;
    data.name = name;
    data.secret = secret;

    // Parse type
    if (type.toUpper() == QStringLiteral("HOTP")) {
        data.type = OathType::HOTP;
    } else if (type.toUpper() == QStringLiteral("TOTP")) {
        data.type = OathType::TOTP;
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Invalid type:" << type;
        return i18n("Invalid credential type (must be TOTP or HOTP)");
    }

    // Parse algorithm
    data.algorithm = algorithmFromString(algorithm);

    data.digits = digits;
    data.period = period;
    data.counter = static_cast<quint32>(counter);
    data.requireTouch = requireTouch;

    // Split name into issuer and account if not already set
    if (data.name.contains(QStringLiteral(":"))) {
        QStringList parts = data.name.split(QStringLiteral(":"));
        if (parts.size() >= 2) {
            data.issuer = parts[0];
            data.account = parts.mid(1).join(QStringLiteral(":"));
        }
    } else {
        data.issuer = data.name;
        data.account = QString();
    }

    // Validate data
    QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Validation failed:" << validationError;
        return validationError;
    }

    // Add credential to device
    auto result = device->addCredential(data);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to add credential:" << result.error();
        return result.error();
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential added successfully";
    return QString(); // Empty string = success
}

bool YubiKeyService::copyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: copyCodeToClipboard" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->copyCodeToClipboard(deviceId, credentialName);
}

bool YubiKeyService::typeCode(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: typeCode" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->typeCode(deviceId, credentialName);
}

void YubiKeyService::onDeviceConnectedInternal(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device connected:" << deviceId;

    // Add to database if not exists
    if (!m_database->hasDevice(deviceId)) {
        const QString deviceName = generateDefaultDeviceName(deviceId);
        m_database->addDevice(deviceId, deviceName, true);
    }

    // Check if device requires password and load it from KWallet
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value() && dbRecord->requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device requires password, loading synchronously from KWallet:" << deviceId;

        // Load password synchronously
        const QString password = m_passwordStorage->loadPasswordSync(deviceId);

        if (!password.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Password loaded successfully, saving in device and fetching credentials";

            // Save password in device for future use
            auto *device = m_deviceManager->getDevice(deviceId);
            if (device) {
                device->setPassword(password);
            }

            // Trigger credential cache update with password
            device->updateCredentialCacheAsync(password);
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: No password in KWallet for device:" << deviceId;
            // Try without password
            auto *dev = m_deviceManager->getDevice(deviceId);
            if (dev) {
                dev->updateCredentialCacheAsync(QString());
            }
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device doesn't require password, fetching credentials";
        auto *dev = m_deviceManager->getDevice(deviceId);
        if (dev) {
            dev->updateCredentialCacheAsync(QString());
        }
    }

    Q_EMIT deviceConnected(deviceId);
}

void YubiKeyService::onDeviceDisconnectedInternal(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device disconnected:" << deviceId;
    Q_EMIT deviceDisconnected(deviceId);
}

void YubiKeyService::onCredentialCacheFetched(const QString &deviceId,
                                             const QList<OathCredential> &credentials)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials updated for device:" << deviceId
                              << "count:" << credentials.size();

    // Only emit if credentials were actually fetched
    if (credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Empty credentials, likely auth failure - NOT emitting credentialsUpdated";
        return;
    }

    Q_EMIT credentialsUpdated(deviceId);
}

void YubiKeyService::clearDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Clearing device from memory:" << deviceId;
    m_deviceManager->removeDeviceFromMemory(deviceId);
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device cleared from memory";
}

QString YubiKeyService::generateDefaultDeviceName(const QString &deviceId) const
{
    return QStringLiteral("YubiKey ") + deviceId;
}

} // namespace YubiKey
} // namespace KRunner
