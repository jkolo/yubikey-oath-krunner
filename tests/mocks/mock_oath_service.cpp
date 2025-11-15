/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mock_oath_service.h"

namespace YubiKeyOath {
namespace Daemon {

MockService::MockService(QObject *parent)
    : QObject(parent)
{
}

QList<DeviceInfo> MockService::listDevices()
{
    return m_devices.values();
}

QList<OathCredential> MockService::getCredentials(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        return getCredentials();
    }

    return m_credentials.value(deviceId);
}

QList<OathCredential> MockService::getCredentials()
{
    QList<OathCredential> allCredentials;

    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        allCredentials.append(it.value());
    }

    return allCredentials;
}

OathDevice* MockService::getDevice(const QString &deviceId)
{
    Q_UNUSED(deviceId);
    return nullptr;  // Not implemented in mock
}

void MockService::addMockDevice(const DeviceInfo &device)
{
    m_devices.insert(device._internalDeviceId, device);
}

void MockService::removeMockDevice(const QString &deviceId)
{
    m_devices.remove(deviceId);
    m_credentials.remove(deviceId);
}

void MockService::addMockCredential(const QString &deviceId, const OathCredential &credential)
{
    if (!m_credentials.contains(deviceId)) {
        m_credentials.insert(deviceId, QList<OathCredential>());
    }

    m_credentials[deviceId].append(credential);
}

void MockService::clearMockCredentials(const QString &deviceId)
{
    m_credentials.remove(deviceId);
}

void MockService::clear()
{
    m_devices.clear();
    m_credentials.clear();
}

int MockService::credentialCount(const QString &deviceId) const
{
    return m_credentials.value(deviceId).size();
}

void MockService::emitDeviceConnected(const QString &deviceId)
{
    Q_EMIT deviceConnected(deviceId);
}

void MockService::emitDeviceDisconnected(const QString &deviceId)
{
    Q_EMIT deviceDisconnected(deviceId);
}

void MockService::emitDeviceForgotten(const QString &deviceId)
{
    Q_EMIT deviceForgotten(deviceId);
}

void MockService::emitCredentialsUpdated(const QString &deviceId)
{
    Q_EMIT credentialsUpdated(deviceId);
}

} // namespace Daemon
} // namespace YubiKeyOath
