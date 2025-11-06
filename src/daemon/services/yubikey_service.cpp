/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_service.h"
#include "../oath/yubikey_device_manager.h"
#include "types/oath_credential_data.h"
#include "../storage/yubikey_database.h"
#include "../storage/secret_storage.h"
#include "../config/daemon_configuration.h"
#include "../actions/yubikey_action_coordinator.h"
#include "../workflows/notification_orchestrator.h"
#include "../logging_categories.h"
#include "../utils/qr_code_parser.h"
#include "../utils/otpauth_uri_parser.h"
#include "../ui/add_credential_dialog.h"
#include "utils/device_name_formatter.h"

#include <QDateTime>
#include <QDebug>
#include <QVariantMap>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

YubiKeyService::YubiKeyService(QObject *parent)
    : QObject(parent)
    , m_deviceManager(std::make_unique<YubiKeyDeviceManager>(nullptr))
    , m_database(std::make_unique<YubiKeyDatabase>(nullptr))
    , m_secretStorage(std::make_unique<SecretStorage>(nullptr))
    , m_config(std::make_unique<DaemonConfiguration>(this))
    , m_actionCoordinator(std::make_unique<YubiKeyActionCoordinator>(this, m_deviceManager.get(), m_database.get(), m_secretStorage.get(), m_config.get(), this))
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
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceForgotten,
            this, &YubiKeyService::deviceForgotten);  // Forward signal directly
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::credentialCacheFetchedForDevice,
            this, &YubiKeyService::onCredentialCacheFetched);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::reconnectStarted,
            this, &YubiKeyService::onReconnectStarted);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::reconnectCompleted,
            this, &YubiKeyService::onReconnectCompleted);
    connect(m_config.get(), &DaemonConfiguration::configurationChanged,
            this, &YubiKeyService::onConfigurationChanged);

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

        // Get firmware version and device model from connected device
        if (info.isConnected) {
            if (auto *device = m_deviceManager->getDevice(deviceId)) {
                info.firmwareVersion = device->firmwareVersion();
                info.deviceModel = device->deviceModel();
            }
        }

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
            const QString storedPassword = m_secretStorage->loadPasswordSync(deviceId);
            info.hasValidPassword = !storedPassword.isEmpty();
        } else {
            info.hasValidPassword = true;
        }

        devices.append(info);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Returning" << devices.size() << "devices";
    return devices;
}

void YubiKeyService::appendCachedCredentialsForOfflineDevice(
    const QString &deviceId,
    QList<OathCredential> &credentialsList)
{
    if (!m_config->enableCredentialsCache()) {
        return;
    }

    // Skip if device is currently connected (already in list)
    if (m_deviceManager->getDevice(deviceId)) {
        return;
    }

    // Get cached credentials for this offline device
    auto cached = m_database->getCredentials(deviceId);
    if (!cached.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Adding" << cached.size()
                                  << "cached credentials for offline device:" << deviceId;
        credentialsList.append(cached);
    }
}

QList<OathCredential> YubiKeyService::getCredentials(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: getCredentials for device:" << deviceId;

    QList<OathCredential> credentials;

    if (deviceId.isEmpty()) {
        // Get from all connected devices
        credentials = m_deviceManager->getCredentials();

        // Add cached credentials for all offline devices
        auto allDevices = m_database->getAllDevices();
        for (const auto &deviceRecord : allDevices) {
            appendCachedCredentialsForOfflineDevice(deviceRecord.deviceId, credentials);
        }
    } else {
        // Get from specific device
        auto *device = m_deviceManager->getDevice(deviceId);
        if (device) {
            credentials = device->credentials();
        } else {
            // Device offline - try cached credentials
            appendCachedCredentialsForOfflineDevice(deviceId, credentials);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Returning" << credentials.size() << "credentials";
    return credentials;
}

// ICredentialUpdateNotifier interface implementation
QList<OathCredential> YubiKeyService::getCredentials()
{
    // Delegate to existing method with empty deviceId (= all devices)
    return getCredentials(QString());
}

YubiKeyOathDevice* YubiKeyService::getDevice(const QString &deviceId)
{
    return m_deviceManager->getDevice(deviceId);
}

QList<QString> YubiKeyService::getConnectedDeviceIds() const
{
    return m_deviceManager->getConnectedDeviceIds();
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

    // Get credential to find its period
    int period = 30; // Default period
    auto credentials = device->credentials();
    for (const auto &cred : credentials) {
        if (cred.originalName == credentialName) {
            period = cred.period;
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Found credential period:" << period;
            break;
        }
    }

    // Calculate validUntil using actual period
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 const timeInPeriod = currentTime % period;
    qint64 const validityRemaining = period - timeInPeriod;
    const qint64 validUntil = currentTime + validityRemaining;

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Generated code, period:" << period
                              << "valid until:" << validUntil;
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

        // FALLBACK: Maybe device doesn't require password at all?
        // Try fetching credentials without password
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Testing if device requires password...";
        device->setPassword(QString());  // Clear password temporarily
        const QList<OathCredential> testCreds = device->fetchCredentialsSync(QString());
        if (!testCreds.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device doesn't require password!";
            m_database->setRequiresPassword(deviceId, false);
            device->updateCredentialCacheAsync(QString());
            return true;  // Success - device doesn't need password
        }

        return false;  // Password really is invalid
    }

    // Save password in device for future use
    device->setPassword(password);

    // Save to KWallet
    if (!m_secretStorage->savePassword(password, deviceId)) {
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

bool YubiKeyService::changePassword(const QString &deviceId,
                                    const QString &oldPassword,
                                    const QString &newPassword)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: changePassword for device:" << deviceId;

    // Get device
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device not found:" << deviceId;
        return false;
    }

    // Change password via OathSession (handles auth + SET_CODE)
    auto changeResult = device->changePassword(oldPassword, newPassword);
    if (changeResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to change password:" << changeResult.error();
        return false;
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Password changed successfully on YubiKey";

    // Update password storage in KWallet
    if (newPassword.isEmpty()) {
        // Password was removed
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Removing password from KWallet";
        m_secretStorage->removePassword(deviceId);

        // Update database - no longer requires password
        m_database->setRequiresPassword(deviceId, false);

        // Clear password from device
        device->setPassword(QString());

        qCInfo(YubiKeyDaemonLog) << "YubiKeyService: Password removed from device" << deviceId;
    } else {
        // Password was changed
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Saving new password to KWallet";
        if (!m_secretStorage->savePassword(newPassword, deviceId)) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to save new password to KWallet";
            // Password changed on YubiKey but not in KWallet - this is a problem
            return false;
        }

        // Update database - still requires password
        m_database->setRequiresPassword(deviceId, true);

        // Update password in device for future operations
        device->setPassword(newPassword);

        qCInfo(YubiKeyDaemonLog) << "YubiKeyService: Password changed on device" << deviceId;
    }

    // Trigger credential cache refresh with new password (or empty if removed)
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Triggering credential cache refresh";
    device->updateCredentialCacheAsync(newPassword);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: changePassword completed successfully";
    return true;
}

void YubiKeyService::forgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: forgetDevice:" << deviceId;

    // IMPORTANT: Order matters to prevent race condition!
    // 1. Remove password from KWallet FIRST (before device is re-detected)
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Removing password from KWallet";
    m_secretStorage->removePassword(deviceId);

    // 2. Remove from database
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Removing device from database";
    m_database->removeDevice(deviceId);

    // 3. Clear device from memory LAST
    // This may trigger immediate re-detection if device is physically connected,
    // but password and database entry are already gone
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Clearing device from memory";
    clearDeviceFromMemory(deviceId);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device forgotten (password, database, memory cleared)";
}

bool YubiKeyService::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: setDeviceName for device:" << deviceId
                              << "new name:" << newName;

    // Validate input
    const QString trimmedName = newName.trimmed();
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
    const bool success = m_database->updateDeviceName(deviceId, trimmedName);

    if (success) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device name updated successfully";
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to update device name in database";
    }

    return success;
}

AddCredentialResult YubiKeyService::addCredential(const QString &deviceId,
                                                  const QString &name,
                                                  const QString &secret,
                                                  const QString &type,
                                                  const QString &algorithm,
                                                  int digits,
                                                  int period,
                                                  int counter,
                                                  bool requireTouch)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: addCredential called - device:" << deviceId
                              << "name:" << name << "hasSecret:" << !secret.isEmpty();

    // Check if we need interactive mode (dialog)
    const bool needsInteractiveMode = deviceId.isEmpty() || name.isEmpty() || secret.isEmpty();

    if (needsInteractiveMode) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Using interactive mode (showing dialog asynchronously)";

        // Prepare initial data with provided parameters (or defaults)
        OathCredentialData initialData;
        if (name.contains(QStringLiteral(":"))) {
            QStringList parts = name.split(QStringLiteral(":"));
            if (parts.size() >= 2) {
                initialData.issuer = parts[0];
                initialData.account = parts.mid(1).join(QStringLiteral(":"));
            }
        } else {
            initialData.issuer = name;
        }
        initialData.name = name;
        initialData.secret = secret;
        initialData.type = type.toUpper() == QStringLiteral("HOTP") ? OathType::HOTP : OathType::TOTP;
        initialData.algorithm = algorithmFromString(algorithm.isEmpty() ? QStringLiteral("SHA1") : algorithm);
        initialData.digits = digits > 0 ? digits : 6;
        initialData.period = period > 0 ? period : 30;
        initialData.counter = counter > 0 ? static_cast<quint32>(counter) : 0;
        initialData.requireTouch = requireTouch;

        // Get available devices
        const QStringList availableDevices = m_deviceManager->getConnectedDeviceIds();
        if (availableDevices.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: No devices available";
            return {QStringLiteral("Error"), i18n("No YubiKey devices connected")};
        }

        // Show dialog asynchronously (non-blocking) - return immediately
        QTimer::singleShot(0, this, [this, deviceId, initialData]() {
            showAddCredentialDialogAsync(deviceId, initialData);
        });

        return {QStringLiteral("Interactive"), i18n("Showing credential dialog")};
    }

    // Automatic mode - all required parameters provided
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Using automatic mode (no dialog)";

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << deviceId << "not found";
        return {QStringLiteral("Error"), i18n("Device not found")};
    }

    // Build credential data from parameters (with defaults for empty values)
    OathCredentialData data;
    data.name = name;
    data.secret = secret;

    // Parse type (default: TOTP)
    const QString typeUpper = type.toUpper();
    if (typeUpper == QStringLiteral("HOTP")) {
        data.type = OathType::HOTP;
    } else if (typeUpper == QStringLiteral("TOTP") || type.isEmpty()) {
        data.type = OathType::TOTP; // Default to TOTP
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Invalid type:" << type;
        return {QStringLiteral("Error"), i18n("Invalid credential type (must be TOTP or HOTP)")};
    }

    // Parse algorithm (default: SHA1)
    data.algorithm = algorithmFromString(algorithm.isEmpty() ? QStringLiteral("SHA1") : algorithm);

    // Apply defaults for numeric parameters
    data.digits = digits > 0 ? digits : 6;           // Default: 6 digits
    data.period = period > 0 ? period : 30;          // Default: 30 seconds
    data.counter = counter > 0 ? static_cast<quint32>(counter) : 0;
    data.requireTouch = requireTouch;

    // Encode period in credential name for TOTP (ykman-compatible format: [period/]issuer:account)
    // Only prepend period if it's non-standard (not 30 seconds)
    if (data.type == OathType::TOTP && data.period != 30) {
        data.name = QString::number(data.period) + QStringLiteral("/") + data.name;
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Encoded period in name:" << data.name;
    }

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
    const QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Validation failed:" << validationError;
        return {QStringLiteral("Error"), validationError};
    }

    // Add credential to device
    auto result = device->addCredential(data);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to add credential:" << result.error();
        return {QStringLiteral("Error"), result.error()};
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential added successfully";
    return {QStringLiteral("Success"), i18n("Credential added successfully")};
}

bool YubiKeyService::deleteCredential(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: deleteCredential" << credentialName << "device:" << deviceId;

    if (credentialName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Empty credential name";
        return false;
    }

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << deviceId << "not found";
        return false;
    }

    // Call deleteCredential on device
    const Result<void> result = device->deleteCredential(credentialName);

    if (result.isSuccess()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential deleted successfully";

        // Emit signal to notify clients that credentials changed
        Q_EMIT credentialsUpdated(deviceId);

        return true;
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to delete credential:" << result.error();
        return false;
    }
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
        const QString password = m_secretStorage->loadPasswordSync(deviceId);

        if (!password.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Password loaded successfully (length:" << password.length() << "), saving in device and fetching credentials";

            // Save password in device for future use
            auto *device = m_deviceManager->getDevice(deviceId);
            if (device) {
                qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Calling setPassword() for device:" << deviceId;
                device->setPassword(password);
                qCDebug(YubiKeyDaemonLog) << "YubiKeyService: setPassword() completed, now calling updateCredentialCacheAsync()";
            } else {
                qCWarning(YubiKeyDaemonLog) << "YubiKeyService: ERROR - device pointer is null for:" << deviceId;
            }

            // Trigger credential cache update with password
            if (device) {
                qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Starting async credential fetch with password for device:" << deviceId;
                device->updateCredentialCacheAsync(password);
            }
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

    // Log credential names for debugging
    if (!credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials list:";
        for (const auto &cred : credentials) {
            qCDebug(YubiKeyDaemonLog) << "  - " << cred.originalName;
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials list is EMPTY";
    }

    // Get device instance to check authentication state
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device disappeared during credential fetch:" << deviceId;
        return;
    }

    // Check if device requires password from database
    bool requiresPassword = false;
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value()) {
        requiresPassword = dbRecord->requiresPassword;
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication state check:";
    qCDebug(YubiKeyDaemonLog) << "  - credentials.isEmpty():" << credentials.isEmpty();
    qCDebug(YubiKeyDaemonLog) << "  - requiresPassword:" << requiresPassword;
    qCDebug(YubiKeyDaemonLog) << "  - device->hasPassword():" << device->hasPassword();

    // AUTHENTICATION DETECTION LOGIC:
    // Empty credentials + requires password + has password = authentication failed (wrong password)
    // Empty credentials + requires password + no password = authentication failed (no password available)
    // Empty credentials + !requires password = device has no credentials (success)
    // Non-empty credentials = authentication succeeded (or not required)

    bool authenticationFailed = false;
    QString authError;

    if (credentials.isEmpty() && requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Empty credentials for password-protected device - authentication failed";
        authenticationFailed = true;

        if (device->hasPassword()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: MARKING AS WRONG PASSWORD - device has password but returned empty credentials";
            authError = tr("Wrong password");
        } else {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: MARKING AS NO PASSWORD - device requires password but none available";
            authError = tr("Password required but not available");
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication check passed - proceeding";
    }

    // AUTO-DETECT: If credentials were fetched successfully without password, device doesn't require password
    if (!authenticationFailed && !device->hasPassword() && !credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Auto-detected - device doesn't require password";
        m_database->setRequiresPassword(deviceId, false);
    }

    // Save credentials to cache if enabled and authentication succeeded
    if (!authenticationFailed && m_config->enableCredentialsCache()) {
        // Rate limiting check - prevent excessive database writes
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int rateLimitMs = m_config->credentialSaveRateLimit();

        if (m_lastCredentialSave.contains(deviceId)) {
            const qint64 timeSinceLastSave = now - m_lastCredentialSave[deviceId];
            if (timeSinceLastSave < rateLimitMs) {
                qCDebug(YubiKeyDaemonLog)
                    << "YubiKeyService: Rate limited credential save for" << deviceId
                    << "- last save" << timeSinceLastSave << "ms ago"
                    << "(limit:" << rateLimitMs << "ms)";

                // Emit appropriate signals based on authentication status
                Q_EMIT credentialsUpdated(deviceId);
                Q_EMIT deviceConnectedAndAuthenticated(deviceId);
                return;
            }
        }

        // Save and update timestamp
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache enabled, saving" << credentials.size() << "credentials";
        if (!m_database->saveCredentials(deviceId, credentials)) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to save credentials to cache";
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials saved to cache successfully";
            m_lastCredentialSave[deviceId] = now;
        }
    } else if (!authenticationFailed) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache disabled, NOT saving to database";
    }

    // EMIT APPROPRIATE SIGNALS based on authentication result
    if (authenticationFailed) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Authentication failed for device:" << deviceId
                                     << "error:" << authError;
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: >>> EMITTING deviceConnectedAuthenticationFailed signal";

        // DO NOT emit credentialsUpdated for auth failures
        // Emit authentication failure signal instead
        Q_EMIT deviceConnectedAuthenticationFailed(deviceId, authError);
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication successful for device:" << deviceId;
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> EMITTING credentialsUpdated and deviceConnectedAndAuthenticated signals";

        // Emit both signals: credentialsUpdated (for backward compat) and deviceConnectedAndAuthenticated (new)
        Q_EMIT credentialsUpdated(deviceId);

        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: this=" << this << "thread=" << QThread::currentThreadId()
                                  << "about to emit deviceConnectedAndAuthenticated for device:" << deviceId;
        Q_EMIT deviceConnectedAndAuthenticated(deviceId);
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: deviceConnectedAndAuthenticated signal emitted successfully";
    }
}

void YubiKeyService::showAddCredentialDialogAsync(const QString &deviceId,
                                                  const OathCredentialData &initialData)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Showing add credential dialog asynchronously";

    // Get available devices (only connected)
    const QList<DeviceInfo> allDevices = listDevices();
    QList<DeviceInfo> availableDevices;

    for (const auto &deviceInfo : allDevices) {
        // Only include connected devices
        if (deviceInfo.isConnected) {
            availableDevices.append(deviceInfo);

            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Available device -"
                                      << "id:" << deviceInfo.deviceId
                                      << "name:" << deviceInfo.deviceName
                                      << "firmware:" << deviceInfo.firmwareVersion.toString()
                                      << "model:" << QString::number(deviceInfo.deviceModel, 16);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Total available devices:" << availableDevices.size();

    // Create dialog on heap (will be deleted by showSaveResult on success, or manually on cancel)
    auto *dialog = new AddCredentialDialog(initialData, availableDevices, deviceId);

    // Connect credentialReadyToSave signal to handle async save
    connect(dialog, &AddCredentialDialog::credentialReadyToSave,
            this, [this, dialog](const OathCredentialData &data, const QString &selectedDeviceId) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential ready to save -"
                                  << "name:" << data.name
                                  << "issuer:" << data.issuer
                                  << "account:" << data.account
                                  << "type:" << (data.type == OathType::TOTP ? "TOTP" : "HOTP")
                                  << "algorithm:" << static_cast<int>(data.algorithm)
                                  << "digits:" << data.digits
                                  << "period:" << data.period
                                  << "requireTouch:" << data.requireTouch
                                  << "secret length:" << data.secret.length()
                                  << "device:" << selectedDeviceId;

        // === SYNCHRONOUS VALIDATION (UI thread - fast) ===

        // Check device exists
        if (selectedDeviceId.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: No device selected";
            dialog->showSaveResult(false, i18n("No device selected"));
            return;
        }

        auto *device = m_deviceManager->getDevice(selectedDeviceId);
        if (!device) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << selectedDeviceId << "not found";
            dialog->showSaveResult(false, i18n("Device not found"));
            return;
        }

        // Check for duplicate credential
        auto existingCredentials = device->credentials();
        for (const auto &cred : existingCredentials) {
            if (cred.originalName == data.name) {
                qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Credential already exists:" << data.name;
                dialog->showSaveResult(false, i18n("Credential with this name already exists on the YubiKey"));
                return;
            }
        }

        // Validate data
        const QString validationError = data.validate();
        if (!validationError.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Validation failed:" << validationError;
            dialog->showSaveResult(false, validationError);
            return;
        }

        // === ASYNCHRONOUS PC/SC OPERATION (background thread) ===

        // Run addCredential in background thread to avoid blocking UI
        // This is especially important for:
        // - PC/SC communication (100-500ms)
        // - Touch-required credentials (user interaction time)
        QFuture<Result<void>> const future = QtConcurrent::run([device, data]() -> Result<void> {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Background thread - starting addCredential";

            // Make copy for modification
            OathCredentialData dialogData = data;

            // Encode period in credential name for TOTP (ykman-compatible format: [period/]issuer:account)
            // Only prepend period if it's non-standard (not 30 seconds)
            if (dialogData.type == OathType::TOTP && dialogData.period != 30) {
                dialogData.name = QString::number(dialogData.period) + QStringLiteral("/") + dialogData.name;
                qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Encoded period in name:" << dialogData.name;
            }

            // PC/SC operation in background thread - this may take 100-500ms
            return device->addCredential(dialogData);
        });

        // Watch future and handle result in UI thread
        auto *watcher = new QFutureWatcher<Result<void>>(this);
        connect(watcher, &QFutureWatcher<Result<void>>::finished,
                this, [this, watcher, dialog, device]() {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Background thread finished";

            auto result = watcher->result();

            if (result.isSuccess()) {
                qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential added successfully";

                // Trigger credential refresh (no password needed for refresh after adding)
                device->updateCredentialCacheAsync();

                // Show success notification if enabled
                if (m_config->showNotifications()) {
                    // TODO: Implement success notification
                    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Would show success notification here";
                }

                // Notify dialog of success (will close and delete itself)
                dialog->showSaveResult(true, QString());
            } else {
                qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to add credential:" << result.error();

                // Show error, keep dialog open for corrections
                dialog->showSaveResult(false, result.error());
            }

            // Clean up watcher
            watcher->deleteLater();
        });

        // Start watching the future
        watcher->setFuture(future);
    });

    // Connect rejected signal to cleanup
    connect(dialog, &QDialog::rejected, this, [dialog]() {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Dialog cancelled";
        dialog->deleteLater();
    });

    // Show dialog (non-blocking)
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void YubiKeyService::clearDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Clearing device from memory:" << deviceId;
    m_deviceManager->removeDeviceFromMemory(deviceId);
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device cleared from memory";
}

QString YubiKeyService::generateDefaultDeviceName(const QString &deviceId) const
{
    return DeviceNameFormatter::generateDefaultName(deviceId);
}

QString YubiKeyService::getDeviceName(const QString &deviceId) const
{
    // Delegate to DeviceNameFormatter for consistent name handling
    return DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database.get());
}

void YubiKeyService::onReconnectStarted(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect started for device:" << deviceId;

    // Show persistent notification that reconnect is in progress
    if (m_config->showNotifications() && m_actionCoordinator) {
        const QString deviceName = getDeviceName(deviceId);
        const QString title = tr("Reconnecting to YubiKey");
        const QString message = tr("Restoring connection to %1...").arg(deviceName);

        // Show persistent notification (no timeout) - will be closed when reconnect completes
        m_reconnectNotificationId = m_actionCoordinator->showPersistentNotification(title, message, 0);
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect notification shown with ID:" << m_reconnectNotificationId;
    }
}

void YubiKeyService::onReconnectCompleted(const QString &deviceId, bool success)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect completed for device:" << deviceId
                              << "success:" << success;

    if (!m_config->showNotifications() || !m_actionCoordinator) {
        return;
    }

    // Get device name for user-friendly notification
    const QString deviceName = getDeviceName(deviceId);

    if (success) {
        // Success - close the reconnecting notification
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect successful, closing notification ID:" << m_reconnectNotificationId;
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;
    } else {
        // Failure - close reconnecting notification and show error
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect failed, closing old notification and showing error";
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;

        const QString title = tr("Reconnect Failed");
        const QString message = tr("Could not restore connection to %1. Please remove and reinsert the YubiKey.").arg(deviceName);

        m_actionCoordinator->showSimpleNotification(title, message, 1);
    }
}

void YubiKeyService::onConfigurationChanged()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Configuration changed";

    // Check if credentials cache was disabled
    if (!m_config->enableCredentialsCache()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache disabled, clearing all cached credentials";
        if (!m_database->clearAllCredentials()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to clear cached credentials";
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: All cached credentials cleared successfully";
        }
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
