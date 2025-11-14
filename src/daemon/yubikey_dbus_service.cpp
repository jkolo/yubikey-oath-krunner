/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "services/yubikey_service.h"
#include "oath/yubikey_device_manager.h"
#include "dbus/oath_manager_object.h"
#include "logging_categories.h"

#include <QDebug>
#include <QDBusConnection>
#include <QTimer>

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
    m_manager = new OathManagerObject(m_service.get(), connection, this);

    if (!m_manager->registerObject()) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyDBusService: Failed to register Manager object - daemon cannot function";
        // Note: Daemon startup will fail if Manager registration fails
        // This is intentional - the daemon is useless without the Manager object
    } else {
        qCInfo(YubiKeyDaemonLog) << "YubiKeyDBusService: Manager object registered successfully";
    }

    // NOTE: Device lifecycle signals are connected in YubiKeyManagerObject constructor
    // - deviceConnected -> addDevice
    // - deviceDisconnected -> onDeviceDisconnected (updates State to Disconnected)
    // - deviceForgotten -> removeDevice (removes from D-Bus completely)

    // Add ALL known devices to Manager (both connected and disconnected from database)
    // (devices detected during YubiKeyService initialization, before signals were connected)
    // Device objects will be created and connected to actual devices if available
    const QList<DeviceInfo> devices = m_service->listDevices();
    for (const auto &devInfo : devices) {
        if (!devInfo._internalDeviceId.isEmpty()) {
            const bool connected = devInfo.isConnected();
            qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Adding device to Manager:"
                                      << devInfo._internalDeviceId << "isConnected:" << connected;
            // Pass connection status - Manager will call connectToDevice() if isConnected=true
            m_manager->addDeviceWithStatus(devInfo._internalDeviceId, connected);
        }
    }

    qCInfo(YubiKeyDaemonLog) << "YubiKeyDBusService: D-Bus interface fully initialized with"
                             << devices.size() << "devices from database";

    // NOW start PC/SC monitoring - D-Bus is ready with all database objects
    // This must happen AFTER D-Bus objects are created to avoid race condition where
    // PC/SC detects cards and triggers updateCredentials() before D-Bus is ready
    QTimer::singleShot(0, this, [this]() {
        qCInfo(YubiKeyDaemonLog) << "YubiKeyDBusService: Starting PC/SC monitoring after D-Bus initialization";
        m_service->getDeviceManager()->startMonitoring();
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: PC/SC monitoring started successfully";
    });

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initialization complete";
}

YubiKeyDBusService::~YubiKeyDBusService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Destructor";
}

} // namespace Daemon
} // namespace YubiKeyOath
