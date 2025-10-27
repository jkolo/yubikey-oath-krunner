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
#include "../utils/screenshot_capture.h"
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
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceForgotten,
            this, &YubiKeyService::deviceForgotten);  // Forward signal directly
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

    // IMPORTANT: Order matters to prevent race condition!
    // 1. Remove password from KWallet FIRST (before device is re-detected)
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Removing password from KWallet";
    m_passwordStorage->removePassword(deviceId);

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

} // namespace Daemon
} // namespace YubiKeyOath
