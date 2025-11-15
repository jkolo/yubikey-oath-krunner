/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_dbus_service.h"
#include "services/oath_service.h"
#include "oath/oath_device_manager.h"
#include "dbus/oath_manager_object.h"
#include "logging_categories.h"

#include <QDebug>
#include <QDBusConnection>
#include <QTimer>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

OathDBusService::OathDBusService(QObject *parent)
    : QObject(parent)
    , m_service(std::make_unique<OathService>(this))
    , m_manager(nullptr)
{
    qCDebug(OathDaemonLog) << "OathDBusService: Initializing D-Bus service with hierarchical architecture";

    // Create and register Manager object at /pl/jkolo/yubikey/oath
    const QDBusConnection connection = QDBusConnection::sessionBus();
    m_manager = new OathManagerObject(m_service.get(), connection, this);

    if (!m_manager->registerObject()) {
        qCCritical(OathDaemonLog) << "OathDBusService: Failed to register Manager object - daemon cannot function";
        // Note: Daemon startup will fail if Manager registration fails
        // This is intentional - the daemon is useless without the Manager object
    } else {
        qCInfo(OathDaemonLog) << "OathDBusService: Manager object registered successfully";
    }

    // NOTE: Device lifecycle signals are connected in YubiKeyManagerObject constructor
    // - deviceConnected -> addDevice
    // - deviceDisconnected -> onDeviceDisconnected (updates State to Disconnected)
    // - deviceForgotten -> removeDevice (removes from D-Bus completely)

    // Add ALL known devices to Manager (both connected and disconnected from database)
    // (devices detected during OathService initialization, before signals were connected)
    // Device objects will be created and connected to actual devices if available
    const QList<DeviceInfo> devices = m_service->listDevices();
    for (const auto &devInfo : devices) {
        if (!devInfo._internalDeviceId.isEmpty()) {
            const bool connected = devInfo.isConnected();
            qCDebug(OathDaemonLog) << "OathDBusService: Adding device to Manager:"
                                      << devInfo._internalDeviceId << "isConnected:" << connected;
            // Pass connection status - Manager will call connectToDevice() if isConnected=true
            m_manager->addDeviceWithStatus(devInfo._internalDeviceId, connected);
        }
    }

    qCInfo(OathDaemonLog) << "OathDBusService: D-Bus interface fully initialized with"
                             << devices.size() << "devices from database";

    // NOW start PC/SC monitoring - D-Bus is ready with all database objects
    // This must happen AFTER D-Bus objects are created to avoid race condition where
    // PC/SC detects cards and triggers updateCredentials() before D-Bus is ready
    QTimer::singleShot(0, this, [this]() {
        qCInfo(OathDaemonLog) << "OathDBusService: Starting PC/SC monitoring after D-Bus initialization";
        m_service->getDeviceManager()->startMonitoring();
        qCDebug(OathDaemonLog) << "OathDBusService: PC/SC monitoring started successfully";
    });

    qCDebug(OathDaemonLog) << "OathDBusService: Initialization complete";
}

OathDBusService::~OathDBusService()
{
    qCDebug(OathDaemonLog) << "OathDBusService: Destructor";
}

} // namespace Daemon
} // namespace YubiKeyOath
