/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "services/yubikey_service.h"
#include "dbus/yubikey_manager_object.h"
#include "logging_categories.h"

#include <QDebug>
#include <QDBusConnection>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

YubiKeyDBusService::YubiKeyDBusService(QObject *parent)
    : QObject(parent)
    , m_service(std::make_unique<YubiKeyService>(this))
    , m_manager(nullptr)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initializing D-Bus service with hierarchical architecture";

    // Create and register Manager object at /pl/jkolo/yubikey/oath
    const QDBusConnection connection = QDBusConnection::sessionBus();
    m_manager = new YubiKeyManagerObject(m_service.get(), connection, this);

    if (!m_manager->registerObject()) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to register Manager object - daemon cannot function";
        // Note: Daemon startup will fail if Manager registration fails
        // This is intentional - the daemon is useless without the Manager object
    } else {
        qCInfo(YubiKeyDaemonLog) << "YubiKeyDBusService: Manager object registered successfully";
    }

    // NOTE: Device lifecycle signals are connected in YubiKeyManagerObject constructor
    // - deviceConnected -> addDevice
    // - deviceDisconnected -> onDeviceDisconnected (updates IsConnected=false)
    // - deviceForgotten -> removeDevice (removes from D-Bus completely)

    // Add ALL known devices to Manager (both connected and disconnected from database)
    // (devices detected during YubiKeyService initialization, before signals were connected)
    // Device objects will be created with correct IsConnected status
    const QList<DeviceInfo> devices = m_service->listDevices();
    for (const auto &devInfo : devices) {
        if (!devInfo.deviceId.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Adding device to Manager:"
                                      << devInfo.deviceId << "isConnected:" << devInfo.isConnected;
            // Pass connection status to ensure correct IsConnected property
            m_manager->addDeviceWithStatus(devInfo.deviceId, devInfo.isConnected);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initialization complete";
}

YubiKeyDBusService::~YubiKeyDBusService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Destructor";
}

} // namespace Daemon
} // namespace YubiKeyOath
