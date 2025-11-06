/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "mock_yubikey_oath_device.h"
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
 * Manages mock YubiKey devices for workflow tests
 */
class MockYubiKeyDeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit MockYubiKeyDeviceManager(QObject *parent = nullptr)
        : QObject(parent)
    {}

    ~MockYubiKeyDeviceManager() override = default;

    // ========== Device Management ==========

    /**
     * @brief Gets device by ID or first available
     */
    MockYubiKeyOathDevice* getDeviceOrFirst(const QString &deviceId = QString())
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
    QList<Shared::OathCredential> getCredentials()
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
    QStringList getConnectedDeviceIds() const
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
