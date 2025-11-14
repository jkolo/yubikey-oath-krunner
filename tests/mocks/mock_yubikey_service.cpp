/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mock_yubikey_service.h"

namespace YubiKeyOath {
namespace Daemon {

MockYubiKeyService::MockYubiKeyService(QObject *parent)
    : QObject(parent)
{
}

QList<DeviceInfo> MockYubiKeyService::listDevices()
{
    return m_devices.values();
}

QList<OathCredential> MockYubiKeyService::getCredentials(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        return getCredentials();
    }

    return m_credentials.value(deviceId);
}

QList<OathCredential> MockYubiKeyService::getCredentials()
{
    QList<OathCredential> allCredentials;

    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        allCredentials.append(it.value());
    }

    return allCredentials;
}

OathDevice* MockYubiKeyService::getDevice(const QString &deviceId)
{
    Q_UNUSED(deviceId);
    return nullptr;  // Not implemented in mock
}

void MockYubiKeyService::addMockDevice(const DeviceInfo &device)
{
    m_devices.insert(device._internalDeviceId, device);
}

void MockYubiKeyService::removeMockDevice(const QString &deviceId)
{
    m_devices.remove(deviceId);
    m_credentials.remove(deviceId);
}

void MockYubiKeyService::addMockCredential(const QString &deviceId, const OathCredential &credential)
{
    if (!m_credentials.contains(deviceId)) {
        m_credentials.insert(deviceId, QList<OathCredential>());
    }

    m_credentials[deviceId].append(credential);
}

void MockYubiKeyService::clearMockCredentials(const QString &deviceId)
{
    m_credentials.remove(deviceId);
}

void MockYubiKeyService::clear()
{
    m_devices.clear();
    m_credentials.clear();
}

int MockYubiKeyService::credentialCount(const QString &deviceId) const
{
    return m_credentials.value(deviceId).size();
}

void MockYubiKeyService::emitDeviceConnected(const QString &deviceId)
{
    Q_EMIT deviceConnected(deviceId);
}

void MockYubiKeyService::emitDeviceDisconnected(const QString &deviceId)
{
    Q_EMIT deviceDisconnected(deviceId);
}

void MockYubiKeyService::emitDeviceForgotten(const QString &deviceId)
{
    Q_EMIT deviceForgotten(deviceId);
}

void MockYubiKeyService::emitCredentialsUpdated(const QString &deviceId)
{
    Q_EMIT credentialsUpdated(deviceId);
}

} // namespace Daemon
} // namespace YubiKeyOath
