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

#include <QDateTime>
#include <QDebug>
#include <QVariantMap>
#include <QTimer>
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
    , m_actionCoordinator(std::make_unique<YubiKeyActionCoordinator>(m_deviceManager.get(), m_database.get(), m_secretStorage.get(), m_config.get(), this))
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
        QList<OathCredential> testCreds = device->fetchCredentialsSync(QString());
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
        QStringList availableDevices = m_deviceManager->getConnectedDeviceIds();
        if (availableDevices.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: No devices available";
            return AddCredentialResult(QStringLiteral("Error"), i18n("No YubiKey devices connected"));
        }

        // Show dialog asynchronously (non-blocking) - return immediately
        QTimer::singleShot(0, this, [this, deviceId, initialData]() {
            showAddCredentialDialogAsync(deviceId, initialData);
        });

        return AddCredentialResult(QStringLiteral("Interactive"), i18n("Showing credential dialog"));
    }

    // Automatic mode - all required parameters provided
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Using automatic mode (no dialog)";

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << deviceId << "not found";
        return AddCredentialResult(QStringLiteral("Error"), i18n("Device not found"));
    }

    // Build credential data from parameters (with defaults for empty values)
    OathCredentialData data;
    data.name = name;
    data.secret = secret;

    // Parse type (default: TOTP)
    QString typeUpper = type.toUpper();
    if (typeUpper == QStringLiteral("HOTP")) {
        data.type = OathType::HOTP;
    } else if (typeUpper == QStringLiteral("TOTP") || type.isEmpty()) {
        data.type = OathType::TOTP; // Default to TOTP
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Invalid type:" << type;
        return AddCredentialResult(QStringLiteral("Error"), i18n("Invalid credential type (must be TOTP or HOTP)"));
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
    QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Validation failed:" << validationError;
        return AddCredentialResult(QStringLiteral("Error"), validationError);
    }

    // Add credential to device
    auto result = device->addCredential(data);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to add credential:" << result.error();
        return AddCredentialResult(QStringLiteral("Error"), result.error());
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential added successfully";
    return AddCredentialResult(QStringLiteral("Success"), i18n("Credential added successfully"));
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
    Result<void> result = device->deleteCredential(credentialName);

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

    // AUTO-DETECT: If credentials were fetched successfully without password, device doesn't require password
    auto *device = m_deviceManager->getDevice(deviceId);
    if (device && !device->hasPassword()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Auto-detected - device doesn't require password";
        m_database->setRequiresPassword(deviceId, false);
    }

    Q_EMIT credentialsUpdated(deviceId);
}

void YubiKeyService::showAddCredentialDialogAsync(const QString &deviceId,
                                                  const OathCredentialData &initialData)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Showing add credential dialog asynchronously";

    // Get available devices with their names
    QStringList deviceIds = m_deviceManager->getConnectedDeviceIds();
    QMap<QString, QString> availableDevices;
    for (const QString &id : deviceIds) {
        // Get device name from database
        auto deviceRecord = m_database->getDevice(id);
        QString displayName = deviceRecord.has_value() ? deviceRecord->deviceName : id;

        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Device" << id
                                  << "has name:" << displayName
                                  << "(from DB:" << (deviceRecord.has_value() ? "yes" : "no") << ")";

        availableDevices.insert(id, displayName);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Available devices map:" << availableDevices;

    // Create dialog on heap (will be deleted automatically when closed)
    auto *dialog = new AddCredentialDialog(initialData, availableDevices, deviceId);

    // Connect accepted signal to add credential
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Dialog accepted, adding credential";

        // Get data from dialog
        OathCredentialData dialogData = dialog->getCredentialData();
        QString selectedDeviceId = dialog->getSelectedDeviceId();

        if (selectedDeviceId.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: No device selected";
            dialog->deleteLater();
            return;
        }

        // Get device instance
        auto *device = m_deviceManager->getDevice(selectedDeviceId);
        if (!device) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device" << selectedDeviceId << "not found";
            dialog->deleteLater();
            return;
        }

        // Validate data
        QString validationError = dialogData.validate();
        if (!validationError.isEmpty()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Validation failed:" << validationError;
            dialog->deleteLater();
            return;
        }

        // Encode period in credential name for TOTP (ykman-compatible format: [period/]issuer:account)
        // Only prepend period if it's non-standard (not 30 seconds)
        if (dialogData.type == OathType::TOTP && dialogData.period != 30) {
            dialogData.name = QString::number(dialogData.period) + QStringLiteral("/") + dialogData.name;
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Encoded period in dialog data name:" << dialogData.name;
        }

        // Add credential to device
        auto result = device->addCredential(dialogData);
        if (result.isError()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to add credential:" << result.error();
            dialog->deleteLater();
            return;
        }

        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credential added successfully via async dialog";

        // Trigger credential refresh (no password needed for refresh after adding)
        device->updateCredentialCacheAsync();

        dialog->deleteLater();
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
    // Use last 8 characters of device ID for shorter, more readable default name
    // Example: "28b5c0b54ccb10db" becomes "YubiKey (...4ccb10db)"
    if (deviceId.length() > 8) {
        const QString shortId = deviceId.right(8);
        return QStringLiteral("YubiKey (...") + shortId + QStringLiteral(")");
    }
    return QStringLiteral("YubiKey (") + deviceId + QStringLiteral(")");
}

QString YubiKeyService::getDeviceName(const QString &deviceId) const
{
    // Try to get custom name from database, fallback to generated default
    auto deviceRecord = m_database->getDevice(deviceId);
    if (deviceRecord.has_value() && !deviceRecord->deviceName.isEmpty()) {
        return deviceRecord->deviceName;
    }
    return generateDefaultDeviceName(deviceId);
}

void YubiKeyService::onReconnectStarted(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect started for device:" << deviceId;

    // Show persistent notification that reconnect is in progress
    if (m_config->showNotifications() && m_actionCoordinator) {
        QString deviceName = getDeviceName(deviceId);
        QString title = tr("Reconnecting to YubiKey");
        QString message = tr("Restoring connection to %1...").arg(deviceName);

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
    QString deviceName = getDeviceName(deviceId);

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

        QString title = tr("Reconnect Failed");
        QString message = tr("Could not restore connection to %1. Please remove and reinsert the YubiKey.").arg(deviceName);

        m_actionCoordinator->showSimpleNotification(title, message, 1);
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
