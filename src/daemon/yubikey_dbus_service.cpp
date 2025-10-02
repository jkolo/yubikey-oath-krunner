/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "oath/yubikey_device_manager.h"
#include "../shared/types/oath_credential.h"
#include "storage/yubikey_database.h"
#include "storage/password_storage.h"
#include "logging_categories.h"

#include <QDateTime>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

YubiKeyDBusService::YubiKeyDBusService(QObject *parent)
    : QObject(parent)
    , m_deviceManager(std::make_unique<YubiKeyDeviceManager>(nullptr))
    , m_database(std::make_unique<YubiKeyDatabase>(nullptr))
    , m_passwordStorage(std::make_unique<PasswordStorage>(nullptr))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initializing";

    // Initialize database
    if (!m_database->initialize()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to initialize database";
    }

    // Initialize OATH
    auto initResult = m_deviceManager->initialize();
    if (initResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to initialize OATH:" << initResult.error();
    }

    // Connect internal signals to D-Bus signals
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceConnected,
            this, &YubiKeyDBusService::onDeviceConnectedInternal);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceDisconnected,
            this, &YubiKeyDBusService::onDeviceDisconnectedInternal);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::credentialCacheFetchedForDevice,
            this, &YubiKeyDBusService::onCredentialCacheFetched);

    // Load passwords for already-connected devices (initialize() may have connected them before signals were connected)
    const QStringList connectedDevices = m_deviceManager->getConnectedDeviceIds();
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Found" << connectedDevices.size() << "already-connected devices";
    for (const QString &deviceId : connectedDevices) {
        // Manually trigger password loading for each connected device
        onDeviceConnectedInternal(deviceId);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initialization complete";
}

YubiKeyDBusService::~YubiKeyDBusService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Destructor";

    if (m_deviceManager) {
        m_deviceManager->cleanup();
    }
}

QList<DeviceInfo> YubiKeyDBusService::ListDevices()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: ListDevices called";

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
            // New device - generate name
            QString shortId = deviceId.left(6);
            if (deviceId.length() > 6) {
                shortId += QStringLiteral("...");
            }
            info.deviceName = QStringLiteral("YubiKey ") + shortId;

            // Default to requiring password (safer)
            info.requiresPassword = true;

            // Add to database
            m_database->addDevice(deviceId, info.deviceName, true);
        }

        // Update last seen for connected devices
        if (info.isConnected) {
            m_database->updateLastSeen(deviceId);
        }

        // Check if we have valid password in KWallet (no authentication needed here)
        if (info.requiresPassword) {
            const QString storedPassword = m_passwordStorage->loadPasswordSync(deviceId);
            info.hasValidPassword = !storedPassword.isEmpty();
        } else {
            // Device doesn't require password
            info.hasValidPassword = true;
        }

        devices.append(info);
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Returning" << devices.size() << "devices";
    return devices;
}

QList<CredentialInfo> YubiKeyDBusService::GetCredentials(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: GetCredentials for device:" << deviceId;

    QList<CredentialInfo> credList;

    // Get credentials from OATH
    QList<OathCredential> credentials;
    QString actualDeviceId = deviceId;

    if (deviceId.isEmpty()) {
        // Use default device
        credentials = m_deviceManager->getCredentials();

        // Get the actual device ID from connected devices list
        QStringList connectedIds = m_deviceManager->getConnectedDeviceIds();
        if (!connectedIds.isEmpty()) {
            actualDeviceId = connectedIds.first();
        }
    } else {
        // Get from specific device
        auto *device = m_deviceManager->getDevice(deviceId);
        if (device) {
            credentials = device->credentials();
        }
    }

    // Convert to D-Bus format
    for (const auto &cred : credentials) {
        credList.append(convertCredential(cred));
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Returning" << credList.size() << "credentials";
    return credList;
}

GenerateCodeResult YubiKeyDBusService::GenerateCode(const QString &deviceId,
                                                     const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: GenerateCode for credential:"
                              << credentialName << "on device:" << deviceId;

    // Get device instance
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Device" << deviceId << "not found";
        return {.code = QString(), .validUntil = 0};
    }

    // Generate code directly on device
    auto result = device->generateCode(credentialName);

    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to generate code:" << result.error();
        return {.code = QString(), .validUntil = 0};
    }

    const QString code = result.value();

    // Calculate validUntil timestamp (30 seconds from now for TOTP)
    const qint64 validUntil = QDateTime::currentSecsSinceEpoch() + 30;

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Generated code, valid until:" << validUntil;
    return {.code = code, .validUntil = validUntil};
}

bool YubiKeyDBusService::SavePassword(const QString &deviceId, const QString &password)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: SavePassword for device:" << deviceId;

    // First test the password by attempting authentication
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Device not found:" << deviceId;
        return false;
    }

    auto authResult = device->authenticateWithPassword(password);
    if (authResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Password is invalid:" << authResult.error();
        return false;
    }

    // Save password in device for future use
    device->setPassword(password);

    // Save to KWallet
    if (!m_passwordStorage->savePassword(password, deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to save password to KWallet";
        return false;
    }

    // Update database flag
    m_database->setRequiresPassword(deviceId, true);

    // Trigger credential cache refresh with the new password
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Password saved, triggering credential cache refresh";
    device->updateCredentialCacheAsync(password);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Password saved successfully";
    return true;
}

void YubiKeyDBusService::ForgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: ForgetDevice:" << deviceId;

    // Clear device from memory BEFORE removing from database
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Clearing device from memory";
    clearDeviceFromMemory(deviceId);

    // Remove from database
    m_database->removeDevice(deviceId);

    // Remove password from KWallet
    m_passwordStorage->removePassword(deviceId);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device forgotten";
}

void YubiKeyDBusService::onDeviceConnectedInternal(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device connected:" << deviceId;

    // Add to database if not exists
    if (!m_database->hasDevice(deviceId)) {
        QString shortId = deviceId.left(6);
        if (deviceId.length() > 6) {
            shortId += QStringLiteral("...");
        }
        const QString deviceName = QStringLiteral("YubiKey ") + shortId;
        m_database->addDevice(deviceId, deviceName, true);
    }

    // Check if device requires password and load it from KWallet
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value() && dbRecord->requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device requires password, loading synchronously from KWallet:" << deviceId;

        // Load password synchronously
        const QString password = m_passwordStorage->loadPasswordSync(deviceId);

        if (!password.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Password loaded successfully, saving in device and fetching credentials";

            // Save password in device for future use
            auto *device = m_deviceManager->getDevice(deviceId);
            if (device) {
                device->setPassword(password);
            }

            // Trigger credential cache update with password
            device->updateCredentialCacheAsync(password);
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: No password in KWallet for device:" << deviceId;
            // Try without password - device might not actually need one, or user needs to set it
            auto *dev = m_deviceManager->getDevice(deviceId);
            if (dev) {
                dev->updateCredentialCacheAsync(QString());
            }
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device doesn't require password, fetching credentials";
        // Device doesn't require password, fetch credentials directly
        auto *dev = m_deviceManager->getDevice(deviceId);
        if (dev) {
            dev->updateCredentialCacheAsync(QString());
        }
    }

    Q_EMIT DeviceConnected(deviceId);
}

void YubiKeyDBusService::onDeviceDisconnectedInternal(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device disconnected:" << deviceId;
    Q_EMIT DeviceDisconnected(deviceId);
}

void YubiKeyDBusService::onCredentialCacheFetched(const QString &deviceId,
                                                 const QList<OathCredential> &credentials)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Credentials updated for device:" << deviceId
                              << "count:" << credentials.size();

    // Only emit CredentialsUpdated if credentials were actually fetched
    // Empty credentials after auth failure should NOT trigger this signal
    if (credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Empty credentials, likely auth failure - NOT emitting CredentialsUpdated";
        return;
    }

    Q_EMIT CredentialsUpdated(deviceId);
}

void YubiKeyDBusService::clearDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Clearing device from memory:" << deviceId;

    // Remove device from YubiKeyOath memory
    m_deviceManager->removeDeviceFromMemory(deviceId);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Device cleared from memory";
}

CredentialInfo YubiKeyDBusService::convertCredential(const OathCredential &credential) const
{
    CredentialInfo info;
    info.name = credential.name;
    info.issuer = credential.issuer;
    info.username = credential.username;
    info.requiresTouch = credential.requiresTouch;

    // Use validUntil timestamp from credential
    // If credential has been generated, it will have a validUntil timestamp
    // Otherwise it will be 0
    info.validUntil = credential.validUntil;

    // Device ID - required for multi-device support
    info.deviceId = credential.deviceId;

    return info;
}

} // namespace YubiKey
} // namespace KRunner
