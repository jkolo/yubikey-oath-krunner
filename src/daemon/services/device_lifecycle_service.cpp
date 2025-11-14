/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_lifecycle_service.h"
#include "../oath/yubikey_device_manager.h"
#include "../oath/oath_device.h"
#include "../storage/yubikey_database.h"
#include "../storage/secret_storage.h"
#include "../logging_categories.h"
#include "utils/device_name_formatter.h"
#include "shared/types/device_model.h"

#include <QSet>
#include <QMetaObject>
#include <QtConcurrent>

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

        // Determine if device is connected and get its state
        const bool isConnected = connectedDeviceIds.contains(deviceId);
        if (isConnected) {
            // Get state from live device object
            if (auto *device = m_deviceManager->getDevice(deviceId)) {
                info.state = device->state();
            } else {
                // Device in connected list but no object available - treat as disconnected
                info.state = Shared::DeviceState::Disconnected;
            }
        } else {
            // Device not connected
            info.state = Shared::DeviceState::Disconnected;
        }

        // Get firmware version, device model, serial number, and form factor from connected device
        if (isConnected) {
            if (auto *device = m_deviceManager->getDevice(deviceId)) {
                info.firmwareVersion = device->firmwareVersion();
                // Use DeviceModel struct fields directly
                const DeviceModel& deviceModel = device->deviceModel();
                info.deviceModel = deviceModel.modelString;
                info.deviceModelCode = deviceModel.modelCode;  // Numeric model code for icon resolution
                info.capabilities = deviceModel.capabilities;
                info.serialNumber = device->serialNumber();
                info.formFactor = formFactorToString(device->formFactor());
                info.requiresPassword = device->requiresPassword();
            }
        }

        // Get from database
        auto dbRecord = m_database->getDevice(deviceId);
        if (dbRecord.has_value()) {
            info.deviceName = DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);
            info.requiresPassword = dbRecord->requiresPassword;
            info.lastSeen = dbRecord->lastSeen;

            // For disconnected devices, populate firmware/model/serial from database cache
            if (!isConnected) {
                info.serialNumber = dbRecord->serialNumber;
                info.firmwareVersion = dbRecord->firmwareVersion;
                info.deviceModel = deviceModelToString(dbRecord->deviceModel);  // Brand-aware conversion
                info.deviceModelCode = dbRecord->deviceModel;
                info.formFactor = formFactorToString(dbRecord->formFactor);
                info.capabilities = capabilitiesToStringList(
                    getModelCapabilities(dbRecord->deviceModel)
                );
            }
        } else {
            // New device - generate name with full device ID
            info.deviceName = generateDefaultDeviceName(deviceId);

            // For offline new devices, default to requiring password (safe default)
            // For connected new devices, requiresPassword was already set from device at line 75
            if (!isConnected) {
                info.requiresPassword = true;
            }

            // Add to database with actual requiresPassword value
            m_database->addDevice(deviceId, info.deviceName, info.requiresPassword);
        }

        // Update last seen for connected devices
        if (isConnected) {
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

OathDevice* DeviceLifecycleService::getDevice(const QString &deviceId)
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

    // 3. Record forget timestamp for debounce (prevents immediate re-detection)
    m_lastForgetTimestamp[deviceId] = QDateTime::currentMSecsSinceEpoch();
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Recorded forget timestamp for debounce";

    // 4. Clear device from memory LAST
    // This may trigger immediate re-detection if device is physically connected,
    // but password and database entry are already gone, and debounce will prevent re-add
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Clearing device from memory";
    clearDeviceFromMemory(deviceId);

    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device forgotten (password, database, memory cleared)";
}

void DeviceLifecycleService::onDeviceConnected(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device connected:" << deviceId;

    // Debounce: ignore re-detection shortly after forget (prevents dev_XXXXXXXX paths)
    // 500ms grace period allows PC/SC state to settle properly
    if (m_lastForgetTimestamp.contains(deviceId)) {
        const qint64 timeSinceForget = QDateTime::currentMSecsSinceEpoch() - m_lastForgetTimestamp[deviceId];
        if (timeSinceForget < 500) {
            qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Ignoring re-detection"
                                       << timeSinceForget << "ms after forget (debounce)";
            return;
        }
        // Grace period expired, clear timestamp
        m_lastForgetTimestamp.remove(deviceId);
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Debounce expired, processing connection";
    }

    // Get device pointer to access requiresPassword
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Device not found in manager:" << deviceId;
        return;
    }

    // Set initial state: Connecting
    device->setState(Shared::DeviceState::Connecting);

    // Check if this is a new device
    bool const isNewDevice = !m_database->hasDevice(deviceId);

    // Add to database if not exists (with temporary device ID as name)
    if (isNewDevice) {
        const QString tempName = generateDefaultDeviceName(deviceId); // Temporary name
        const bool requiresPassword = device->requiresPassword();
        m_database->addDevice(deviceId, tempName, requiresPassword);
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: New device added to database with requiresPassword:" << requiresPassword;
    }

    // Update extended device information (firmware, model, serial, form factor)
    // This syncs hardware data from YubiKey to database
    const bool updateSuccess = m_database->updateDeviceInfo(
        deviceId,
        device->firmwareVersion(),
        device->deviceModel().modelCode,
        device->serialNumber(),
        device->formFactor()
    );

    if (updateSuccess) {
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Updated device info in database:"
                                   << "firmware=" << device->firmwareVersion().toString()
                                   << "model=" << device->deviceModel().modelString
                                   << "serial=" << device->serialNumber()
                                   << "formFactor=" << device->formFactor();

        // Always regenerate device name to support brand-aware migration (v1.1.0)
        // This ensures devices added before multi-brand support get correct names
        // (e.g., Nitrokey previously stored as "YubiKey 4" → "Nitrokey 3C NFC - 562721119")
        const QString properName = generateDefaultDeviceName(
            deviceId,
            device->deviceModel(),
            device->serialNumber(),
            m_database
        );

        // Update name only if it changed (avoid unnecessary DB writes)
        // This also preserves custom user names if they manually edited them to match the generated format
        auto dbRecord = m_database->getDevice(deviceId);
        if (!dbRecord.has_value() || dbRecord->deviceName != properName) {
            m_database->updateDeviceName(deviceId, properName);
            qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Updated device name to:" << properName;
        }
    } else {
        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: Failed to update device info in database for:" << deviceId;
    }

    // Check if device requires password and load it from KWallet
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value() && dbRecord->requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device requires password, loading ASYNCHRONOUSLY from KWallet:" << deviceId;

        // Set device state to Authenticating (password loading phase)
        device->setState(Shared::DeviceState::Authenticating);

        // Load password asynchronously to avoid blocking daemon startup
        [[maybe_unused]] auto future = QtConcurrent::run([this, deviceId, device]() {
            qCDebug(YubiKeyDaemonLog) << "[Worker] Loading password from KWallet for device:" << deviceId;

            const QString password = m_secretStorage->loadPasswordSync(deviceId);

            // Process result on main thread
            QMetaObject::invokeMethod(this, [this, deviceId, device, password]() {
                if (!password.isEmpty()) {
                    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Password loaded successfully from KWallet";

                    // Save password in device for future use
                    if (device) {
                        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Calling setPassword() for device:" << deviceId;
                        device->setPassword(password);
                    } else {
                        qCWarning(YubiKeyDaemonLog) << "DeviceLifecycleService: ERROR - device pointer is null for:" << deviceId;
                        return;
                    }

                    // Trigger credential cache update with password
                    if (device) {
                        qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Starting async credential fetch with password for device:" << deviceId;
                        device->updateCredentialCacheAsync(password);
                    }
                } else {
                    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: No password in KWallet for device:" << deviceId;
                    // Try without password
                    if (device) {
                        device->updateCredentialCacheAsync(QString());
                    }
                }
            }, Qt::QueuedConnection);
        });
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

    // removeDeviceFromMemory() will delete the device object
    // Destructor handles PC/SC cleanup automatically
    m_deviceManager->removeDeviceFromMemory(deviceId);
    qCDebug(YubiKeyDaemonLog) << "DeviceLifecycleService: Device cleared from memory";
}

QString DeviceLifecycleService::generateDefaultDeviceName(const QString &deviceId) const
{
    return DeviceNameFormatter::generateDefaultName(deviceId);
}

QString DeviceLifecycleService::generateDefaultDeviceName(const QString &deviceId,
                                                          const Shared::DeviceModel& deviceModel,
                                                          quint32 serialNumber,
                                                          YubiKeyDatabase *database) const
{
    return DeviceNameFormatter::generateDefaultName(deviceId, deviceModel, serialNumber, database);
}

QString DeviceLifecycleService::getDeviceName(const QString &deviceId) const
{
    // Delegate to DeviceNameFormatter for consistent name handling
    return DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);
}

} // namespace Daemon
} // namespace YubiKeyOath
