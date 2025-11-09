/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_lifecycle_service.h"
#include "../oath/yubikey_device_manager.h"
#include "../oath/yubikey_oath_device.h"
#include "../storage/yubikey_database.h"
#include "../storage/secret_storage.h"
#include "../logging_categories.h"
#include "utils/device_name_formatter.h"
#include "types/yubikey_model.h"

#include <QSet>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

DeviceLifecycleService::DeviceLifecycleService(YubiKeyDeviceManager *deviceManager,
                                             YubiKeyDatabase *database,
                                             SecretStorage *secretStorage,
                                             QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_secretStorage(secretStorage)
{
    Q_ASSERT(m_deviceManager);
    Q_ASSERT(m_database);
    Q_ASSERT(m_secretStorage);

    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Initialized";
}

QList<DeviceInfo> DeviceLifecycleService::listDevices()
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: listDevices called";

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
        info._internalDeviceId = deviceId;
        info.isConnected = connectedDeviceIds.contains(deviceId);

        // Get firmware version, device model, serial number, and form factor from connected device
        if (info.isConnected) {
            if (auto *device = m_deviceManager->getDevice(deviceId)) {
                info.firmwareVersion = device->firmwareVersion();
                // Convert raw values to human-readable strings
                const YubiKeyModel rawModel = device->deviceModel();
                info.deviceModel = modelToString(rawModel);
                info.deviceModelCode = rawModel;  // Numeric model code for icon resolution
                info.capabilities = capabilitiesToStringList(getModelCapabilities(rawModel));
                info.serialNumber = device->serialNumber();
                info.formFactor = formFactorToString(device->formFactor());
            }
        }

        // Get from database
        auto dbRecord = m_database->getDevice(deviceId);
        if (dbRecord.has_value()) {
            info.deviceName = DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);
            info.requiresPassword = dbRecord->requiresPassword;
            info.lastSeen = dbRecord->lastSeen;

            // For disconnected devices, populate firmware/model/serial from database cache
            if (!info.isConnected) {
                info.serialNumber = dbRecord->serialNumber;
                info.firmwareVersion = dbRecord->firmwareVersion;
                info.deviceModel = modelToString(dbRecord->deviceModel);
                info.deviceModelCode = dbRecord->deviceModel;
                info.formFactor = formFactorToString(dbRecord->formFactor);
                info.capabilities = capabilitiesToStringList(
                    getModelCapabilities(dbRecord->deviceModel)
                );
            }
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

    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Returning" << devices.size() << "devices";
    return devices;
}

YubiKeyOathDevice* DeviceLifecycleService::getDevice(const QString &deviceId)
{
    return m_deviceManager->getDevice(deviceId);
}

QList<QString> DeviceLifecycleService::getConnectedDeviceIds() const
{
    return m_deviceManager->getConnectedDeviceIds();
}

QDateTime DeviceLifecycleService::getDeviceLastSeen(const QString &deviceId) const
{
    auto deviceRecord = m_database->getDevice(deviceId);
    if (deviceRecord.has_value()) {
        return deviceRecord->lastSeen;
    }
    return {}; // Invalid QDateTime if device not in database
}

bool DeviceLifecycleService::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: setDeviceName for device:" << deviceId
                              << "new name:" << newName;

    // Validate input
    const QString trimmedName = newName.trimmed();
    if (deviceId.isEmpty() || trimmedName.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Invalid device ID or name (empty after trim)";
        return false;
    }

    // Validate name length (max 64 chars)
    if (trimmedName.length() > 64) {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Name too long (max 64 chars)";
        return false;
    }

    // Check if device exists in database
    if (!m_database->hasDevice(deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Device not found in database:" << deviceId;
        return false;
    }

    // Update device name in database
    const bool success = m_database->updateDeviceName(deviceId, trimmedName);

    if (success) {
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device name updated successfully";
    } else {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Failed to update device name in database";
    }

    return success;
}

void DeviceLifecycleService::forgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: forgetDevice:" << deviceId;

    // IMPORTANT: Order matters to prevent race condition!
    // 1. Remove password from KWallet FIRST (before device is re-detected)
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Removing password from KWallet";
    m_secretStorage->removePassword(deviceId);

    // 2. Remove from database
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Removing device from database";
    if (!m_database->removeDevice(deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Failed to remove device from database:" << deviceId;
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Continuing with memory cleanup despite database failure";
    }

    // 3. Clear device from memory LAST
    // This may trigger immediate re-detection if device is physically connected,
    // but password and database entry are already gone
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Clearing device from memory";
    clearDeviceFromMemory(deviceId);

    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device forgotten (password, database, memory cleared)";
}

void DeviceLifecycleService::onDeviceConnected(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device connected:" << deviceId;

    // Check if this is a new device
    bool const isNewDevice = !m_database->hasDevice(deviceId);

    // Add to database if not exists (with temporary device ID as name)
    if (isNewDevice) {
        const QString tempName = generateDefaultDeviceName(deviceId); // Temporary name
        m_database->addDevice(deviceId, tempName, true);
    }

    // Update extended device information (firmware, model, serial, form factor)
    // This syncs hardware data from YubiKey to database
    auto *device = m_deviceManager->getDevice(deviceId);
    if (device) {
        const bool updateSuccess = m_database->updateDeviceInfo(
            deviceId,
            device->firmwareVersion(),
            device->deviceModel(),
            device->serialNumber(),
            device->formFactor()
        );

        if (updateSuccess) {
            qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Updated device info in database:"
                                       << "firmware=" << device->firmwareVersion().toString()
                                       << "model=" << QString::number(device->deviceModel(), 16)
                                       << "serial=" << device->serialNumber()
                                       << "formFactor=" << device->formFactor();

            // For new devices, generate proper name based on model and serial
            if (isNewDevice) {
                const QString properName = generateDefaultDeviceName(
                    deviceId,
                    device->deviceModel(),
                    device->serialNumber(),
                    m_database
                );
                m_database->updateDeviceName(deviceId, properName);
                qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Set device name to:" << properName;
            }
        } else {
            qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Failed to update device info in database for:" << deviceId;
        }
    } else {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Device not found in manager, cannot sync extended info:" << deviceId;
    }

    // Check if device requires password and load it from KWallet
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value() && dbRecord->requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device requires password, loading synchronously from KWallet:" << deviceId;

        // Load password synchronously
        const QString password = m_secretStorage->loadPasswordSync(deviceId);

        if (!password.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Password loaded successfully from KWallet, saving in device and fetching credentials";

            // Save password in device for future use (reuse device pointer from above)
            if (device) {
                qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Calling setPassword() for device:" << deviceId;
                device->setPassword(password);
                qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: setPassword() completed, now calling updateCredentialCacheAsync()";
            } else {
                qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: ERROR - device pointer is null for:" << deviceId;
            }

            // Trigger credential cache update with password
            if (device) {
                qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Starting async credential fetch with password for device:" << deviceId;
                device->updateCredentialCacheAsync(password);
            }
        } else {
            qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: No password in KWallet for device:" << deviceId;
            // Try without password
            auto *dev = m_deviceManager->getDevice(deviceId);
            if (dev) {
                dev->updateCredentialCacheAsync(QString());
            }
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device doesn't require password, fetching credentials";
        auto *dev = m_deviceManager->getDevice(deviceId);
        if (dev) {
            dev->updateCredentialCacheAsync(QString());
        }
    }

    Q_EMIT deviceConnected(deviceId);
}

void DeviceLifecycleService::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device disconnected:" << deviceId;

    // Update last seen timestamp in database
    m_database->updateLastSeen(deviceId);

    Q_EMIT deviceDisconnected(deviceId);
}

void DeviceLifecycleService::clearDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Clearing device from memory:" << deviceId;
    m_deviceManager->removeDeviceFromMemory(deviceId);
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device cleared from memory";
}

QString DeviceLifecycleService::generateDefaultDeviceName(const QString &deviceId) const
{
    return DeviceNameFormatter::generateDefaultName(deviceId);
}

QString DeviceLifecycleService::generateDefaultDeviceName(const QString &deviceId,
                                                          YubiKeyModel model,
                                                          quint32 serialNumber,
                                                          YubiKeyDatabase *database) const
{
    return DeviceNameFormatter::generateDefaultName(deviceId, model, serialNumber, database);
}

QString DeviceLifecycleService::getDeviceName(const QString &deviceId) const
{
    // Delegate to DeviceNameFormatter for consistent name handling
    return DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);
}

} // namespace Daemon
} // namespace YubiKeyOath
