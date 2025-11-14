/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_service.h"
#include "../oath/yubikey_device_manager.h"
#include "../oath/oath_device.h"
#include "../storage/yubikey_database.h"
#include "../../shared/config/configuration_provider.h"
#include "../logging_categories.h"
#include "../ui/add_credential_dialog.h"
#include "../notification/dbus_notification_manager.h"
#include "utils/device_name_formatter.h"

#include <QTimer>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QtConcurrent/QtConcurrent>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

CredentialService::CredentialService(YubiKeyDeviceManager *deviceManager,
                                   YubiKeyDatabase *database,
                                   Shared::ConfigurationProvider *config,
                                   QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_config(config)
    , m_notificationManager(std::make_unique<DBusNotificationManager>(this))
{
    Q_ASSERT(m_deviceManager);
    Q_ASSERT(m_database);
    Q_ASSERT(m_config);

    qCDebug(YubiKeyDaemonLog) << "CredentialService: Initialized";
}

CredentialService::~CredentialService() = default;

void CredentialService::appendCachedCredentialsForOfflineDevice(
    const QString &deviceId,
    QList<OathCredential> &credentialsList)
{
    if (!m_config->enableCredentialsCache()) {
        return;
    }

    // Skip if device is currently connected AND credentials already in list
    // (but allow if credentials list is empty - device connected but not initialized yet)
    if (m_deviceManager->getDevice(deviceId) && !credentialsList.isEmpty()) {
        return;
    }

    // Get cached credentials for this offline device (or connected but not initialized)
    auto cached = m_database->getCredentials(deviceId);
    if (!cached.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Adding" << cached.size()
                                  << "cached credentials for offline device:" << deviceId;
        credentialsList.append(cached);
    }
}

QList<OathCredential> CredentialService::getCredentials(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: getCredentials for device:" << deviceId;

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
            // If device is connected but credentials not yet loaded in memory, fall back to cache
            if (credentials.isEmpty()) {
                qCDebug(YubiKeyDaemonLog) << "CredentialService: Device connected but credentials not in memory, using database cache";
                appendCachedCredentialsForOfflineDevice(deviceId, credentials);
            }
        } else {
            // Device offline - try cached credentials
            appendCachedCredentialsForOfflineDevice(deviceId, credentials);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "CredentialService: Returning" << credentials.size() << "credentials";
    return credentials;
}

QList<OathCredential> CredentialService::getCredentials()
{
    // Delegate to existing method with empty deviceId (= all devices)
    return getCredentials(QString());
}

GenerateCodeResult CredentialService::generateCode(const QString &deviceId,
                                                   const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: generateCode for credential:"
                              << credentialName << "on device:" << deviceId;

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << deviceId << "not found";
        return {.code = QString(), .validUntil = 0};
    }

    // Generate code directly on device
    auto result = device->generateCode(credentialName);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Failed to generate code:" << result.error();
        return {.code = QString(), .validUntil = 0};
    }

    const QString code = result.value();

    // Get credential to find its period
    int period = 30; // Default period
    auto credentials = device->credentials();
    for (const auto &cred : credentials) {
        if (cred.originalName == credentialName) {
            period = cred.period;
            qCDebug(YubiKeyDaemonLog) << "CredentialService: Found credential period:" << period;
            break;
        }
    }

    // Calculate validUntil using actual period
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 const timeInPeriod = currentTime % period;
    qint64 const validityRemaining = period - timeInPeriod;
    const qint64 validUntil = currentTime + validityRemaining;

    qCDebug(YubiKeyDaemonLog) << "CredentialService: Generated code, period:" << period
                              << "valid until:" << validUntil;
    return {.code = code, .validUntil = validUntil};
}

AddCredentialResult CredentialService::addCredential(const QString &deviceId,
                                                    const QString &name,
                                                    const QString &secret,
                                                    const QString &type,
                                                    const QString &algorithm,
                                                    int digits,
                                                    int period,
                                                    int counter,
                                                    bool requireTouch)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: addCredential called - device:" << deviceId
                              << "name:" << name << "hasSecret:" << !secret.isEmpty();

    // Check if we need interactive mode (dialog)
    const bool needsInteractiveMode = deviceId.isEmpty() || name.isEmpty() || secret.isEmpty();

    if (needsInteractiveMode) {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Using interactive mode (showing dialog asynchronously)";

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

        // Show dialog directly (avoids race condition with D-Bus call return)
        // Dialog will handle device availability and wait for connection if needed
        showAddCredentialDialogAsync(deviceId, initialData);

        return {QStringLiteral("Interactive"), i18n("Showing credential dialog")};
    }

    // Automatic mode - all required parameters provided
    qCDebug(YubiKeyDaemonLog) << "CredentialService: Using automatic mode (no dialog)";

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << deviceId << "not found";
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
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Invalid type:" << type;
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
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Encoded period in name:" << data.name;
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
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Validation failed:" << validationError;
        return {QStringLiteral("Error"), validationError};
    }

    // Add credential to device
    auto result = device->addCredential(data);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Failed to add credential:" << result.error();
        return {QStringLiteral("Error"), result.error()};
    }

    qCDebug(YubiKeyDaemonLog) << "CredentialService: Credential added successfully";
    return {QStringLiteral("Success"), i18n("Credential added successfully")};
}

bool CredentialService::deleteCredential(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: deleteCredential" << credentialName << "device:" << deviceId;

    if (credentialName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Empty credential name";
        return false;
    }

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << deviceId << "not found";
        return false;
    }

    // Call deleteCredential on device
    const Result<void> result = device->deleteCredential(credentialName);

    if (result.isSuccess()) {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Credential deleted successfully";

        // Emit signal to notify clients that credentials changed
        Q_EMIT credentialsUpdated(deviceId);

        return true;
    } else {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Failed to delete credential:" << result.error();
        return false;
    }
}

// === ASYNC API IMPLEMENTATIONS ===

void CredentialService::generateCodeAsync(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: generateCodeAsync for credential:"
                              << credentialName << "on device:" << deviceId;

    // Validate input
    if (deviceId.isEmpty() || credentialName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Invalid parameters (empty deviceId or credentialName)";
        Q_EMIT codeGenerated(deviceId, credentialName, QString(), 0,
                           i18n("Invalid parameters: deviceId and credentialName cannot be empty"));
        return;
    }

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << deviceId << "not found";
        Q_EMIT codeGenerated(deviceId, credentialName, QString(), 0, i18n("Device not found"));
        return;
    }

    // Run PC/SC operation in background thread to avoid blocking
    [[maybe_unused]] auto future = QtConcurrent::run([this, device, deviceId, credentialName]() {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: [Worker] Generating code for:" << credentialName;

        // PC/SC operation (100-500ms, or longer if touch required)
        auto result = device->generateCode(credentialName);

        // Get credential to find its period
        int period = 30; // Default period
        auto credentials = device->credentials();
        for (const auto &cred : credentials) {
            if (cred.originalName == credentialName) {
                period = cred.period;
                break;
            }
        }

        // Calculate validUntil
        qint64 validUntil = 0;
        QString code;
        QString error;

        if (result.isSuccess()) {
            code = result.value();
            qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
            qint64 const timeInPeriod = currentTime % period;
            qint64 const validityRemaining = period - timeInPeriod;
            validUntil = currentTime + validityRemaining;
            qCDebug(YubiKeyDaemonLog) << "CredentialService: [Worker] Code generated, valid until:" << validUntil;
        } else {
            error = result.error();
            qCWarning(YubiKeyDaemonLog) << "CredentialService: [Worker] Failed to generate code:" << error;
        }

        // Emit result on main thread
        QMetaObject::invokeMethod(this, [this, deviceId, credentialName, code, validUntil, error]() {
            Q_EMIT codeGenerated(deviceId, credentialName, code, validUntil, error);
        }, Qt::QueuedConnection);
    });
}

void CredentialService::deleteCredentialAsync(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: deleteCredentialAsync" << credentialName << "device:" << deviceId;

    // Validate input
    if (credentialName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Empty credential name";
        Q_EMIT credentialDeleted(deviceId, credentialName, false, i18n("Credential name cannot be empty"));
        return;
    }

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << deviceId << "not found";
        Q_EMIT credentialDeleted(deviceId, credentialName, false, i18n("Device not found"));
        return;
    }

    // Run PC/SC operation in background thread to avoid blocking
    [[maybe_unused]] auto future = QtConcurrent::run([this, device, deviceId, credentialName]() {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: [Worker] Deleting credential:" << credentialName;

        // PC/SC operation (100-500ms)
        const Result<void> result = device->deleteCredential(credentialName);

        bool success = false;
        QString error;

        if (result.isSuccess()) {
            success = true;
            qCDebug(YubiKeyDaemonLog) << "CredentialService: [Worker] Credential deleted successfully";
        } else {
            error = result.error();
            qCWarning(YubiKeyDaemonLog) << "CredentialService: [Worker] Failed to delete credential:" << error;
        }

        // Emit result on main thread
        QMetaObject::invokeMethod(this, [this, deviceId, credentialName, success, error]() {
            Q_EMIT credentialDeleted(deviceId, credentialName, success, error);

            // Also emit credentialsUpdated if successful
            if (success) {
                Q_EMIT credentialsUpdated(deviceId);
            }
        }, Qt::QueuedConnection);
    });
}

void CredentialService::showAddCredentialDialogAsync(const QString &deviceId,
                                                     const OathCredentialData &initialData)
{
    qCDebug(YubiKeyDaemonLog) << "CredentialService: Showing add credential dialog asynchronously";

    // Get all available devices (connected and disconnected)
    const QList<DeviceInfo> availableDevices = getAvailableDevices();

    // Create dialog on heap (will be deleted by showSaveResult on success, or manually on cancel)
    auto *dialog = new AddCredentialDialog(initialData, availableDevices, deviceId);

    // Keep dialog alive in active dialogs list (important for disconnected device waiting)
    m_activeDialogs.append(dialog);

    // Remove from list when dialog is destroyed
    connect(dialog, &QObject::destroyed, this, [this, dialog]() {
        m_activeDialogs.removeAll(dialog);
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Dialog destroyed, removed from active list";
    });

    // Connect credentialReadyToSave signal to handle async save
    connect(dialog, &AddCredentialDialog::credentialReadyToSave,
            this, [this, dialog](const OathCredentialData &data, const QString &selectedDeviceId) {
        qCDebug(YubiKeyDaemonLog) << "CredentialService: Credential ready to save -"
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

        // === SINGLE CODE PATH: Check device connection ===
        auto *device = m_deviceManager->getDevice(selectedDeviceId);

        if (!device) {
            // Device NOT connected - wait for connection
            qCDebug(YubiKeyDaemonLog) << "CredentialService: Device not connected, waiting for connection:" << selectedDeviceId;

            // Update dialog overlay
            dialog->updateOverlayStatus(i18n("Waiting for device connection..."));

            // Connect to deviceConnected signal and wait
            auto *connection = new QMetaObject::Connection();
            *connection = connect(m_deviceManager, &YubiKeyDeviceManager::deviceConnected,
                    this, [this, dialog, data, selectedDeviceId, connection](const QString &deviceId) {
                if (deviceId == selectedDeviceId) {
                    qCDebug(YubiKeyDaemonLog) << "CredentialService: Device connected:" << deviceId;

                    // Update overlay
                    dialog->updateOverlayStatus(i18n("Device connected - saving credential..."));

                    // Disconnect signal to avoid multiple triggers
                    disconnect(*connection);
                    delete connection;

                    // Now validate and save
                    QString errorMessage;
                    auto *device = validateCredentialBeforeSave(data, selectedDeviceId, errorMessage);
                    if (!device) {
                        dialog->showSaveResult(false, errorMessage);
                        return;
                    }

                    // === ASYNCHRONOUS PC/SC OPERATION (background thread) ===
                    QFuture<Result<void>> const future = QtConcurrent::run([device, data]() -> Result<void> {
                        qCDebug(YubiKeyDaemonLog) << "CredentialService: Background thread - starting addCredential";

                        // Make copy for modification
                        OathCredentialData dialogData = data;

                        // Encode period in credential name for TOTP (ykman-compatible format: [period/]issuer:account)
                        if (dialogData.type == OathType::TOTP && dialogData.period != 30) {
                            dialogData.name = QString::number(dialogData.period) + QStringLiteral("/") + dialogData.name;
                            qCDebug(YubiKeyDaemonLog) << "CredentialService: Encoded period in name:" << dialogData.name;
                        }

                        // PC/SC operation in background thread
                        return device->addCredential(dialogData);
                    });

                    // Watch future and handle result in UI thread
                    auto *watcher = new QFutureWatcher<Result<void>>(this);
                    connect(watcher, &QFutureWatcher<Result<void>>::finished,
                            this, [this, watcher, dialog, device, data]() {
                        qCDebug(YubiKeyDaemonLog) << "CredentialService: Background thread finished";

                        auto result = watcher->result();

                        if (result.isSuccess()) {
                            qCDebug(YubiKeyDaemonLog) << "CredentialService: Credential added successfully";

                            // Trigger credential refresh
                            device->updateCredentialCacheAsync();

                            // Show success notification if enabled
                            if (m_config->showNotifications()) {
                                m_notificationManager->showNotification(
                                    i18n("YubiKey OATH"),
                                    0,
                                    QStringLiteral("yubikey"),
                                    i18n("Credential Added"),
                                    i18n("Credential '%1' has been added successfully").arg(data.name),
                                    QStringList(),
                                    QVariantMap(),
                                    5000
                                );
                            }

                            // Show success in dialog
                            dialog->showSaveResult(true, i18n("Credential added successfully"));

                            // Emit signal
                            Q_EMIT credentialsUpdated(device->deviceId());
                        } else {
                            qCWarning(YubiKeyDaemonLog) << "CredentialService: Failed to add credential:" << result.error();
                            dialog->showSaveResult(false, result.error());
                        }

                        watcher->deleteLater();
                    });

                    watcher->setFuture(future);
                }
            });

            return;  // Exit early - wait for device
        }

        // === Device connected - validate and save ===
        QString errorMessage;
        device = validateCredentialBeforeSave(data, selectedDeviceId, errorMessage);
        if (!device) {
            dialog->showSaveResult(false, errorMessage);
            return;
        }

        // === ASYNCHRONOUS PC/SC OPERATION (background thread) ===

        // Run addCredential in background thread to avoid blocking UI
        // This is especially important for:
        // - PC/SC communication (100-500ms)
        // - Touch-required credentials (user interaction time)
        QFuture<Result<void>> const future = QtConcurrent::run([device, data]() -> Result<void> {
            qCDebug(YubiKeyDaemonLog) << "CredentialService: Background thread - starting addCredential";

            // Make copy for modification
            OathCredentialData dialogData = data;

            // Encode period in credential name for TOTP (ykman-compatible format: [period/]issuer:account)
            // Only prepend period if it's non-standard (not 30 seconds)
            if (dialogData.type == OathType::TOTP && dialogData.period != 30) {
                dialogData.name = QString::number(dialogData.period) + QStringLiteral("/") + dialogData.name;
                qCDebug(YubiKeyDaemonLog) << "CredentialService: Encoded period in name:" << dialogData.name;
            }

            // PC/SC operation in background thread - this may take 100-500ms
            return device->addCredential(dialogData);
        });

        // Watch future and handle result in UI thread
        auto *watcher = new QFutureWatcher<Result<void>>(this);
        connect(watcher, &QFutureWatcher<Result<void>>::finished,
                this, [this, watcher, dialog, device, data]() {
            qCDebug(YubiKeyDaemonLog) << "CredentialService: Background thread finished";

            auto result = watcher->result();

            if (result.isSuccess()) {
                qCDebug(YubiKeyDaemonLog) << "CredentialService: Credential added successfully";

                // Trigger credential refresh (no password needed for refresh after adding)
                device->updateCredentialCacheAsync();

                // Show success notification if enabled
                if (m_config->showNotifications()) {
                    m_notificationManager->showNotification(
                        i18n("YubiKey OATH"),
                        0,  // replacesId - 0 for new notification
                        QStringLiteral("yubikey"),
                        i18n("Credential Added"),
                        i18n("Credential '%1' has been added successfully").arg(data.name),
                        QStringList(),  // actions
                        QVariantMap(),  // hints
                        5000  // 5 second timeout
                    );
                }

                // Show success in dialog (will auto-close and delete dialog)
                dialog->showSaveResult(true, i18n("Credential added successfully"));

                // Emit signal to notify that credentials changed
                Q_EMIT credentialsUpdated(device->deviceId());
            } else {
                qCWarning(YubiKeyDaemonLog) << "CredentialService: Failed to add credential:" << result.error();

                // Show error in dialog
                dialog->showSaveResult(false, result.error());
            }

            watcher->deleteLater();
        });

        watcher->setFuture(future);
    });

    // Ensure dialog is visible and on top (important for daemon processes without main window)
    dialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
    dialog->show();
    dialog->activateWindow();
    dialog->raise();
}

QList<DeviceInfo> CredentialService::getAvailableDevices()
{
    // Get all devices from database (includes firmware/model info)
    const QList<YubiKeyDatabase::DeviceRecord> allDeviceRecords = m_database->getAllDevices();

    // Get currently connected device IDs
    const QList<QString> connectedIds = m_deviceManager->getConnectedDeviceIds();

    QList<DeviceInfo> availableDevices;

    // Build DeviceInfo for each device (connected or not)
    for (const auto &record : allDeviceRecords) {
        DeviceInfo deviceInfo;
        deviceInfo._internalDeviceId = record.deviceId;
        // Set state based on connection status (simplified - actual state comes from device manager)
        deviceInfo.state = connectedIds.contains(record.deviceId) ? Shared::DeviceState::Ready : Shared::DeviceState::Disconnected;
        deviceInfo.deviceName = DeviceNameFormatter::getDeviceDisplayName(record.deviceId, m_database);
        deviceInfo.firmwareVersion = record.firmwareVersion;
        deviceInfo.deviceModel = modelToString(record.deviceModel);
        deviceInfo.serialNumber = record.serialNumber;
        deviceInfo.formFactor = formFactorToString(record.formFactor);

        availableDevices.append(deviceInfo);

        qCDebug(YubiKeyDaemonLog) << "CredentialService: Available device -"
                                  << "id:" << deviceInfo._internalDeviceId
                                  << "name:" << deviceInfo.deviceName
                                  << "connected:" << deviceInfo.isConnected()
                                  << "firmware:" << deviceInfo.firmwareVersion.toString()
                                  << "model:" << deviceInfo.deviceModel;
    }

    qCDebug(YubiKeyDaemonLog) << "CredentialService: Total available devices:" << availableDevices.size();
    return availableDevices;
}

OathDevice* CredentialService::validateCredentialBeforeSave(const OathCredentialData &data,
                                                                    const QString &selectedDeviceId,
                                                                    QString &errorMessage)
{
    // Check device exists
    if (selectedDeviceId.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: No device selected";
        errorMessage = i18n("No device selected");
        return nullptr;
    }

    auto *device = m_deviceManager->getDevice(selectedDeviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "CredentialService: Device" << selectedDeviceId << "not found";
        errorMessage = i18n("Device not found");
        return nullptr;
    }

    // Check for duplicate credential
    auto existingCredentials = device->credentials();
    for (const auto &cred : existingCredentials) {
        if (cred.originalName == data.name) {
            qCWarning(YubiKeyDaemonLog) << "CredentialService: Credential already exists:" << data.name;
            errorMessage = i18n("Credential with this name already exists on the YubiKey");
            return nullptr;
        }
    }

    // Validation passed
    return device;
}

} // namespace Daemon
} // namespace YubiKeyOath
