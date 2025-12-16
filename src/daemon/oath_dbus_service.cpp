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
#include <QDBusMetaType>
#include <QTimer>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

void OathDBusService::registerDBusTypes()
{
    // Register D-Bus types only once (static flag pattern)
    static bool registered = false;
    if (registered) {
        return;
    }

    // Register all custom types used in D-Bus communication
    // These types are used by Manager, Device, and Credential interfaces
    qDBusRegisterMetaType<DeviceInfo>();
    qDBusRegisterMetaType<CredentialInfo>();
    qDBusRegisterMetaType<GenerateCodeResult>();
    qDBusRegisterMetaType<AddCredentialResult>();
    qDBusRegisterMetaType<QList<DeviceInfo>>();
    qDBusRegisterMetaType<QList<CredentialInfo>>();
    qDBusRegisterMetaType<DeviceState>();

    // Register ObjectManager types (used by GetManagedObjects)
    qDBusRegisterMetaType<InterfacePropertiesMap>();
    qDBusRegisterMetaType<ManagedObjectMap>();

    registered = true;
    qCDebug(OathDaemonLog) << "OathDBusService: D-Bus metatypes registered";
}

OathDBusService::OathDBusService(QObject *parent)
    : QObject(parent)
    , m_service(std::make_unique<OathService>(this))
    , m_manager(nullptr)
{
    // Register D-Bus types before any D-Bus operations
    registerDBusTypes();

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
    // - deviceConnected -> addDevice (creates D-Bus objects for devices)
    // - deviceDisconnected -> onDeviceDisconnected (updates State to Disconnected)
    // - deviceForgotten -> removeDevice (removes from D-Bus completely)
    //
    // Device initialization from database happens in OathService via signal emission
    // (see oath_service.cpp constructor - QTimer::singleShot for deviceConnected signals)

    qCInfo(OathDaemonLog) << "OathDBusService: D-Bus interface initialized (devices will be added via signals)";

    // Start PC/SC monitoring after D-Bus infrastructure is ready
    // Device D-Bus objects will be created asynchronously via deviceConnected signals
    QTimer::singleShot(0, this, [this]() {
        qCInfo(OathDaemonLog) << "OathDBusService: Starting PC/SC monitoring";
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
