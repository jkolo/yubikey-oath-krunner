/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include "types/yubikey_value_types.h"
#include "types/oath_credential.h"

namespace YubiKeyOath {
namespace Daemon {
    class YubiKeyDeviceManager;
    class CredentialService;
    class OathDevice;
}
}

namespace YubiKeyOath {
namespace Daemon {

using namespace YubiKeyOath::Shared;

/**
 * @brief Mock implementation of YubiKeyService for testing D-Bus objects
 *
 * Provides lightweight mock of YubiKeyService without requiring actual
 * PC/SC hardware or dependencies. Stores devices and credentials in memory.
 *
 * Used in tests for OathManagerObject, OathDeviceObject, OathCredentialObject.
 */
class MockYubiKeyService : public QObject
{
    Q_OBJECT

public:
    explicit MockYubiKeyService(QObject *parent = nullptr);
    ~MockYubiKeyService() override = default;

    // ========================================================================
    // YubiKeyService API (subset used by D-Bus objects)
    // ========================================================================

    /**
     * @brief Lists all mock devices
     * @return List of device information
     */
    QList<DeviceInfo> listDevices();

    /**
     * @brief Gets credentials for specific device
     * @param deviceId Device ID (empty = all devices)
     * @return List of credentials
     */
    QList<OathCredential> getCredentials(const QString &deviceId);

    /**
     * @brief Gets all credentials from all devices
     * @return List of credentials from all devices
     */
    QList<OathCredential> getCredentials();

    /**
     * @brief Gets device instance by ID (always returns nullptr in mock)
     * @param deviceId Device ID
     * @return nullptr (not implemented in mock)
     */
    OathDevice* getDevice(const QString &deviceId);

    /**
     * @brief Gets device manager (always returns nullptr in mock)
     * @return nullptr (not needed for D-Bus object tests)
     */
    YubiKeyDeviceManager* getDeviceManager() const { return nullptr; }

    /**
     * @brief Gets credential service (always returns nullptr in mock)
     * @return nullptr (not needed for D-Bus object tests)
     */
    CredentialService* getCredentialService() const { return nullptr; }

    // ========================================================================
    // Test Helper API
    // ========================================================================

    /**
     * @brief Adds a mock device
     * @param device Device information to add
     *
     * Stores device in memory. Does NOT emit signals automatically.
     * Test must call emitDeviceConnected() manually.
     */
    void addMockDevice(const DeviceInfo &device);

    /**
     * @brief Removes a mock device
     * @param deviceId Device ID to remove
     *
     * Removes device and all its credentials. Does NOT emit signals automatically.
     * Test must call emitDeviceForgotten() manually.
     */
    void removeMockDevice(const QString &deviceId);

    /**
     * @brief Adds a mock credential to device
     * @param deviceId Device ID
     * @param credential Credential to add
     */
    void addMockCredential(const QString &deviceId, const OathCredential &credential);

    /**
     * @brief Removes all mock credentials from device
     * @param deviceId Device ID
     */
    void clearMockCredentials(const QString &deviceId);

    /**
     * @brief Clears all mock data
     */
    void clear();

    /**
     * @brief Gets number of mock devices
     * @return Device count
     */
    int deviceCount() const { return m_devices.size(); }

    /**
     * @brief Gets number of credentials for device
     * @param deviceId Device ID
     * @return Credential count
     */
    int credentialCount(const QString &deviceId) const;

    /**
     * @brief Manually emits deviceConnected signal
     * @param deviceId Device ID
     */
    void emitDeviceConnected(const QString &deviceId);

    /**
     * @brief Manually emits deviceDisconnected signal
     * @param deviceId Device ID
     */
    void emitDeviceDisconnected(const QString &deviceId);

    /**
     * @brief Manually emits deviceForgotten signal
     * @param deviceId Device ID
     */
    void emitDeviceForgotten(const QString &deviceId);

    /**
     * @brief Manually emits credentialsUpdated signal
     * @param deviceId Device ID
     */
    void emitCredentialsUpdated(const QString &deviceId);

Q_SIGNALS:
    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID
     */
    void credentialsUpdated(const QString &deviceId);

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

    /**
     * @brief Emitted when a device is forgotten
     * @param deviceId Device ID
     */
    void deviceForgotten(const QString &deviceId);

private:
    // Map: deviceId -> DeviceInfo
    QMap<QString, DeviceInfo> m_devices;

    // Map: deviceId -> list of credentials
    QMap<QString, QList<OathCredential>> m_credentials;
};

} // namespace Daemon
} // namespace YubiKeyOath
