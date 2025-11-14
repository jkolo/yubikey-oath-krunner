/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "mock_yubikey_oath_device.h"
#include "daemon/oath/yubikey_device_manager.h"
#include "types/oath_credential.h"
#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of YubiKeyDeviceManager for testing
 *
 * Manages mock YubiKey devices for workflow tests.
 * Inherits from YubiKeyDeviceManager to be compatible with services
 * that expect YubiKeyDeviceManager*, but uses mock devices internally.
 */
class MockYubiKeyDeviceManager : public YubiKeyDeviceManager
{
    Q_OBJECT

public:
    explicit MockYubiKeyDeviceManager(QObject *parent = nullptr)
        : YubiKeyDeviceManager(parent)
    {}

    ~MockYubiKeyDeviceManager() override = default;

    // ========== Device Management ==========

    /**
     * @brief Gets device by ID (overrides base class)
     * @param deviceId Device identifier
     * @return Device pointer or nullptr if not found
     */
    OathDevice* getDevice(const QString &deviceId) override
    {
        if (m_devices.contains(deviceId)) {
            return m_devices[deviceId];
        }
        return nullptr;
    }

    /**
     * @brief Gets device by ID with concrete type
     * @param deviceId Device identifier
     * @return Mock device pointer or nullptr if not found
     */
    MockYubiKeyOathDevice* getMockDevice(const QString &deviceId)
    {
        if (m_devices.contains(deviceId)) {
            return m_devices[deviceId];
        }
        return nullptr;
    }

    /**
     * @brief Gets mock device by ID or first available
     */
    MockYubiKeyOathDevice* getMockDeviceOrFirst(const QString &deviceId = QString())
    {
        if (!deviceId.isEmpty() && m_devices.contains(deviceId)) {
            return m_devices[deviceId];
        }

        if (!m_devices.isEmpty()) {
            return m_devices.first();
        }

        return nullptr;
    }

    /**
     * @brief Gets all credentials from all devices
     */
    QList<Shared::OathCredential> getCredentials() override
    {
        QList<Shared::OathCredential> allCredentials;
        for (auto *device : m_devices) {
            allCredentials.append(device->credentials());
        }
        return allCredentials;
    }

    /**
     * @brief Gets list of connected device IDs
     */
    QStringList getConnectedDeviceIds() const override
    {
        return m_devices.keys();
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Adds mock device
     */
    void addDevice(MockYubiKeyOathDevice *device)
    {
        m_devices[device->deviceId()] = device;
        device->setParent(this);
        Q_EMIT deviceConnected(device->deviceId());
    }

    /**
     * @brief Removes mock device
     */
    void removeDevice(const QString &deviceId)
    {
        if (m_devices.contains(deviceId)) {
            auto *device = m_devices.take(deviceId);
            Q_EMIT deviceDisconnected(deviceId);
            device->deleteLater();
        }
    }

    /**
     * @brief Removes device from memory (for forgetDevice workflow)
     * Alias for removeDevice() - same behavior in mock
     */
    void removeDeviceFromMemory(const QString &deviceId) override
    {
        removeDevice(deviceId);
    }

    /**
     * @brief Creates and adds test device with credentials
     */
    MockYubiKeyOathDevice* createTestDevice(
        const QString &deviceId,
        const QList<Shared::OathCredential> &credentials = QList<Shared::OathCredential>()
    )
    {
        auto *device = new MockYubiKeyOathDevice(deviceId, this);
        device->setCredentials(credentials);
        addDevice(device);
        return device;
    }

    /**
     * @brief Clears all devices
     */
    void reset()
    {
        QStringList ids = m_devices.keys();
        for (const QString &id : ids) {
            removeDevice(id);
        }
    }

    /**
     * @brief Gets device count
     */
    int deviceCount() const
    {
        return m_devices.size();
    }

Q_SIGNALS:
    /**
     * @brief Emitted when device is connected
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when device is disconnected
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when credentials are updated
     */
    void credentialsUpdated(const QString &deviceId);

    /**
     * @brief Emitted when code is generated successfully
     */
    void codeGenerated(const QString &credentialName, const QString &code);

    /**
     * @brief Emitted when code generation fails
     */
    void codeGenerationFailed(const QString &credentialName, const QString &error);

private:
    QMap<QString, MockYubiKeyOathDevice*> m_devices;
};

} // namespace Daemon
} // namespace YubiKeyOath
