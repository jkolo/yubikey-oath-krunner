/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_reconnect_coordinator.h"
#include "../logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

DeviceReconnectCoordinator::DeviceReconnectCoordinator(QObject *parent)
    : QObject(parent)
{
}

DeviceReconnectCoordinator::~DeviceReconnectCoordinator()
{
    cancel();
}

void DeviceReconnectCoordinator::setReconnectFunction(ReconnectFunction func)
{
    m_reconnectFunc = std::move(func);
}

void DeviceReconnectCoordinator::startReconnect(const QString &deviceId,
                                                 const QString &readerName,
                                                 const QByteArray &command)
{
    qCDebug(OathDeviceManagerLog) << "DeviceReconnectCoordinator::startReconnect() for device"
                                     << deviceId << "reader:" << readerName
                                     << "command length:" << command.length();

    // Cancel any existing reconnection
    cancel();

    // CRITICAL: Copy parameters to local variables
    // References may point to fields of objects that could be deleted
    m_deviceId = deviceId;
    m_readerName = readerName;
    m_command = command;

    // Emit signal that reconnection started
    Q_EMIT reconnectStarted(m_deviceId);

    // Create timer for initial delay
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &DeviceReconnectCoordinator::onTimeout);

    qCDebug(OathDeviceManagerLog) << "Starting reconnect with" << INITIAL_DELAY_MS << "ms initial delay";
    m_timer->start(INITIAL_DELAY_MS);
}

void DeviceReconnectCoordinator::cancel()
{
    if (m_timer) {
        qCDebug(OathDeviceManagerLog) << "Cancelling reconnect for device" << m_deviceId;
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    cleanup();
}

bool DeviceReconnectCoordinator::isReconnecting() const
{
    return m_timer != nullptr;
}

QString DeviceReconnectCoordinator::currentDeviceId() const
{
    return m_deviceId;
}

void DeviceReconnectCoordinator::onTimeout()
{
    qCDebug(OathDeviceManagerLog) << "DeviceReconnectCoordinator::onTimeout() for device"
                                     << m_deviceId << "reader:" << m_readerName;

    // Check if reconnect function is set
    if (!m_reconnectFunc) {
        qCWarning(OathDeviceManagerLog) << "Reconnect function not set - failing";
        const QString deviceIdCopy = m_deviceId;
        cleanup();
        Q_EMIT reconnectCompleted(deviceIdCopy, false);
        return;
    }

    // Try to reconnect (device has exponential backoff built-in)
    qCDebug(OathDeviceManagerLog) << "Calling reconnect function for device" << m_deviceId;
    const auto result = m_reconnectFunc(m_readerName);

    // Cleanup timer
    if (m_timer) {
        delete m_timer;
        m_timer = nullptr;
    }

    // Emit result
    const QString deviceIdCopy = m_deviceId;
    const bool success = result.isSuccess();

    if (success) {
        qCInfo(OathDeviceManagerLog) << "Reconnect successful for device" << m_deviceId;
    } else {
        qCWarning(OathDeviceManagerLog) << "Reconnect failed for device" << m_deviceId
                                           << "error:" << result.error();
    }

    cleanup();
    Q_EMIT reconnectCompleted(deviceIdCopy, success);
}

void DeviceReconnectCoordinator::cleanup()
{
    m_deviceId.clear();
    m_readerName.clear();
    m_command.clear();
}

} // namespace Daemon
} // namespace YubiKeyOath
