/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include "types/yubikey_value_types.h"
#include "shared/types/device_model.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathDeviceManager;
class OathDatabase;
class SecretStorage;
class OathDevice;

/**
 * @brief Service responsible for YubiKey device lifecycle management
 *
 * Handles device enumeration, connection, disconnection, naming, and removal.
 * Coordinates between hardware detection, database persistence, and password storage.
 *
 * Extracted from OathService to follow Single Responsibility Principle.
 */
class DeviceLifecycleService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct DeviceLifecycleService
     * @param deviceManager Device manager for accessing YubiKey hardware
     * @param database Database for persisting device metadata
     * @param secretStorage KWallet storage for loading passwords
     * @param parent Parent QObject
     */
    explicit DeviceLifecycleService(OathDeviceManager *deviceManager,
                                   OathDatabase *database,
                                   SecretStorage *secretStorage,
                                   QObject *parent = nullptr);

    ~DeviceLifecycleService() override = default;

    /**
     * @brief Lists all known YubiKey devices (connected + database)
     * @return List of device information
     *
     * Merges connected devices with database records, generating
     * default names for new devices.
     */
    QList<Shared::DeviceInfo> listDevices();

    /**
     * @brief Gets device instance by ID
     * @param deviceId Device ID to retrieve
     * @return Pointer to device or nullptr if not found
     */
    OathDevice* getDevice(const QString &deviceId);

    /**
     * @brief Gets IDs of all currently connected devices
     * @return List of connected device IDs
     */
    QList<QString> getConnectedDeviceIds() const;

    /**
     * @brief Gets last seen timestamp for device
     * @param deviceId Device ID
     * @return Last seen timestamp or invalid QDateTime if not in database
     */
    QDateTime getDeviceLastSeen(const QString &deviceId) const;

    /**
     * @brief Sets custom name for device
     * @param deviceId Device ID
     * @param newName New device name (max 64 chars)
     * @return true if name updated successfully
     */
    bool setDeviceName(const QString &deviceId, const QString &newName);

    /**
     * @brief Removes device from system (database, KWallet, memory)
     * @param deviceId Device ID
     *
     * Order matters to prevent race conditions:
     * 1. Remove password from KWallet
     * 2. Remove from database
     * 3. Clear from memory (may trigger re-detection)
     */
    void forgetDevice(const QString &deviceId);

    /**
     * @brief Gets device name (custom or generated default)
     * @param deviceId Device ID
     * @return Custom name from database or generated default
     */
    QString getDeviceName(const QString &deviceId) const;

Q_SIGNALS:
    /**
     * @brief Emitted when a device is connected
     * @param deviceId Device ID
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when a device is disconnected
     * @param deviceId Device ID
     */
    void deviceDisconnected(const QString &deviceId);

public Q_SLOTS:
    /**
     * @brief Handles device connection events
     * @param deviceId Device ID
     *
     * Performs:
     * - Database initialization for new devices
     * - Firmware/model/serial sync
     * - Name generation
     * - Password loading from KWallet
     * - Credential cache initialization
     */
    void onDeviceConnected(const QString &deviceId);

    /**
     * @brief Handles device disconnection events
     * @param deviceId Device ID
     *
     * Updates last seen timestamp in database.
     */
    void onDeviceDisconnected(const QString &deviceId);

private:
    /**
     * @brief Clears device from memory
     * @param deviceId Device ID
     */
    void clearDeviceFromMemory(const QString &deviceId);

    /**
     * @brief Generates default device name from device ID (legacy fallback)
     * @param deviceId Device ID
     * @return "YubiKey (...<deviceId>)"
     */
    QString generateDefaultDeviceName(const QString &deviceId) const;

    /**
     * @brief Generates default device name from model and serial
     * @param deviceId Device ID (fallback if model unknown)
     * @param deviceModel Device model with brand and model string
     * @param serialNumber Device serial number (0 if unavailable)
     * @param database Database for checking duplicate names
     * @return "{BRAND} {MODEL} - {SERIAL}" or "{BRAND} {MODEL} {N}"
     */
    QString generateDefaultDeviceName(const QString &deviceId,
                                      const Shared::DeviceModel& deviceModel,
                                      quint32 serialNumber,
                                      OathDatabase *database) const;

    OathDeviceManager *m_deviceManager;  // Not owned
    OathDatabase *m_database;            // Not owned
    SecretStorage *m_secretStorage;         // Not owned

    QMap<QString, qint64> m_lastForgetTimestamp;  ///< Debounce: timestamp of last forget per device
};

} // namespace Daemon
} // namespace YubiKeyOath
