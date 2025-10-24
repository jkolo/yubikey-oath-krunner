/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "services/yubikey_service.h"
#include "actions/yubikey_action_coordinator.h"
#include "dbus/type_conversions.h"
#include "logging_categories.h"

#include <QDebug>

namespace KRunner {
namespace YubiKey {

YubiKeyDBusService::YubiKeyDBusService(QObject *parent)
    : QObject(parent)
    , m_service(std::make_unique<YubiKeyService>(this))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initializing thin D-Bus marshaling layer";

    // Direct signal-to-signal connections from service to D-Bus (no forwarding slots needed)
    connect(m_service.get(), &YubiKeyService::deviceConnected,
            this, &YubiKeyDBusService::DeviceConnected);
    connect(m_service.get(), &YubiKeyService::deviceDisconnected,
            this, &YubiKeyDBusService::DeviceDisconnected);
    connect(m_service.get(), &YubiKeyService::credentialsUpdated,
            this, &YubiKeyDBusService::CredentialsUpdated);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Initialization complete";
}

YubiKeyDBusService::~YubiKeyDBusService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDBusService: Destructor";
}

QList<DeviceInfo> YubiKeyDBusService::ListDevices()
{
    // Pure D-Bus marshaling - delegate to service
    return m_service->listDevices();
}

QList<CredentialInfo> YubiKeyDBusService::GetCredentials(const QString &deviceId)
{
    // Delegate to service and convert to D-Bus types
    QList<OathCredential> credentials = m_service->getCredentials(deviceId);

    QList<CredentialInfo> credList;
    for (const auto &cred : credentials) {
        credList.append(TypeConversions::toCredentialInfo(cred));
    }

    return credList;
}

GenerateCodeResult YubiKeyDBusService::GenerateCode(const QString &deviceId,
                                                     const QString &credentialName)
{
    // Pure delegation - already returns D-Bus type
    return m_service->generateCode(deviceId, credentialName);
}

bool YubiKeyDBusService::SavePassword(const QString &deviceId, const QString &password)
{
    // Pure delegation
    return m_service->savePassword(deviceId, password);
}

void YubiKeyDBusService::ForgetDevice(const QString &deviceId)
{
    // Pure delegation
    m_service->forgetDevice(deviceId);
}

bool YubiKeyDBusService::SetDeviceName(const QString &deviceId, const QString &newName)
{
    // Pure delegation
    return m_service->setDeviceName(deviceId, newName);
}

QString YubiKeyDBusService::AddCredential(const QString &deviceId,
                                          const QString &name,
                                          const QString &secret,
                                          const QString &type,
                                          const QString &algorithm,
                                          int digits,
                                          int period,
                                          int counter,
                                          bool requireTouch)
{
    // Pure delegation
    return m_service->addCredential(deviceId, name, secret, type, algorithm,
                                   digits, period, counter, requireTouch);
}

bool YubiKeyDBusService::CopyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    // Pure delegation to service layer
    return m_service->copyCodeToClipboard(deviceId, credentialName);
}

bool YubiKeyDBusService::TypeCode(const QString &deviceId, const QString &credentialName)
{
    // Pure delegation to service layer
    return m_service->typeCode(deviceId, credentialName);
}

} // namespace YubiKey
} // namespace KRunner
