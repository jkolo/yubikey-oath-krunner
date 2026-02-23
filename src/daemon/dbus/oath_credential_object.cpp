/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_credential_object.h"
#include "services/oath_service.h"
#include "services/credential_service.h"
#include "actions/oath_action_coordinator.h"
#include "actions/action_executor.h"  // For ActionExecutor::ActionResult
#include "oath/oath_device.h"  // For OathDevice::touchRequired() signal
#include "logging_categories.h"
#include "credentialadaptor.h"  // Auto-generated D-Bus adaptor

#include <QDBusConnection>
#include <QDBusError>
#include <QElapsedTimer>
#include <utility>

namespace YubiKeyOath {
namespace Daemon {

static constexpr const char *CREDENTIAL_INTERFACE = "pl.jkolo.yubikey.oath.Credential";

OathCredentialObject::OathCredentialObject(Shared::OathCredential credential,
                                                 QString deviceId,
                                                 OathService *service,
                                                 QDBusConnection connection,
                                                 QObject *parent)
    : QObject(parent)
    , m_credential(std::move(credential))
    , m_deviceId(std::move(deviceId))
    , m_service(service)
    , m_connection(std::move(connection))
    , m_registered(false)
{
    // Object path will be set by parent (YubiKeyDeviceObject)
    // For now, construct it here for logging
    // Format: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
    // credentialId is encoded by parent

    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Constructing for credential:"
                              << m_credential.originalName << "on device:" << m_deviceId;

    // Create D-Bus adaptor for Credential interface
    // This automatically registers pl.jkolo.yubikey.oath.Credential interface
    new CredentialAdaptor(this);
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: CredentialAdaptor created";
}

OathCredentialObject::~OathCredentialObject()
{
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Destructor for credential:"
                              << m_credential.originalName;
    disconnectPending();
    unregisterObject();
}

void OathCredentialObject::setObjectPath(const QString &path)
{
    if (m_registered) {
        qCWarning(OathDaemonLog) << "YubiKeyCredentialObject: Cannot change path after registration";
        return;
    }
    m_objectPath = path;
}

bool OathCredentialObject::registerObject()
{
    if (m_registered) {
        qCWarning(OathDaemonLog) << "YubiKeyCredentialObject: Already registered:"
                                    << m_credential.originalName;
        return true;
    }

    // Object path must be set by parent before calling registerObject()
    if (m_objectPath.isEmpty()) {
        qCCritical(OathDaemonLog) << "YubiKeyCredentialObject: Cannot register - no object path set";
        return false;
    }

    // Register on D-Bus using adaptor (exports interfaces defined in XML)
    // Using ExportAdaptors ensures we use the interface name from the adaptor's Q_CLASSINFO,
    // not the C++ class name
    if (!m_connection.registerObject(m_objectPath, this, QDBusConnection::ExportAdaptors)) {
        qCCritical(OathDaemonLog) << "YubiKeyCredentialObject: Failed to register at"
                                     << m_objectPath << ":" << m_connection.lastError().message();
        return false;
    }

    m_registered = true;
    qCInfo(OathDaemonLog) << "YubiKeyCredentialObject: Registered successfully:"
                             << m_credential.originalName << "at" << m_objectPath;

    return true;
}

void OathCredentialObject::unregisterObject()
{
    if (!m_registered) {
        return;
    }

    m_connection.unregisterObject(m_objectPath);
    m_registered = false;
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Unregistered:" << m_credential.originalName;
}

QString OathCredentialObject::objectPath() const
{
    return m_objectPath;
}

QString OathCredentialObject::fullName() const
{
    return m_credential.originalName;
}

QString OathCredentialObject::issuer() const
{
    return m_credential.issuer;
}

QString OathCredentialObject::username() const
{
    return m_credential.account;
}

bool OathCredentialObject::requiresTouch() const
{
    return m_credential.requiresTouch;
}

QString OathCredentialObject::type() const
{
    return m_credential.type == OathType::TOTP
           ? QString::fromLatin1("TOTP")
           : QString::fromLatin1("HOTP");
}

QString OathCredentialObject::algorithm() const
{
    return algorithmToString(m_credential.algorithm);
}

int OathCredentialObject::digits() const
{
    return m_credential.digits;
}

int OathCredentialObject::period() const
{
    return m_credential.period;
}

QString OathCredentialObject::deviceId() const
{
    return m_deviceId;
}

// === ASYNC API IMPLEMENTATION ===

void OathCredentialObject::disconnectPending()
{
    if (m_pendingConnection) {
        disconnect(m_pendingConnection);
        m_pendingConnection = {};
    }
    if (m_touchSignalConnection) {
        disconnect(m_touchSignalConnection);
        m_touchSignalConnection = {};
    }
}

void OathCredentialObject::executeWithCodeGeneration(bool handleTouch, const CodeResultCallback &onResult)
{
    const bool showTouch = handleTouch && m_credential.requiresTouch;

    // Cancel any previous pending operation (must be before touch signal setup)
    disconnectPending();

    if (showTouch) {
        auto *device = m_service->getDevice(m_deviceId);
        if (device) {
            // Capture values now - device may be destroyed later
            const Shared::DeviceModel deviceModel = device->deviceModel();
            const int timeout = m_service->getActionCoordinator()->touchTimeout();
            const QString credentialName = m_credential.originalName;

            // Show notification when device emits preemptive touchRequired() signal
            // (fired from worker thread just before CALCULATE APDU - LED blinks immediately after)
            m_touchSignalConnection = connect(device, &OathDevice::touchRequired,
                this, [this, credentialName, timeout, deviceModel]() {
                    if (m_touchSignalConnection) {
                        disconnect(m_touchSignalConnection);
                        m_touchSignalConnection = {};
                    }
                    m_service->getActionCoordinator()->showTouchNotification(
                        credentialName, timeout, deviceModel);
                });
        }
    }

    m_pendingConnection = connect(m_service->getCredentialService(), &CredentialService::codeGenerated,
            this, [this, onResult, showTouch](const QString &deviceId, const QString &credentialName,
                                              const QString &code, qint64 validUntil, const QString &error) {
        if (deviceId == m_deviceId && credentialName == m_credential.originalName) {
            disconnectPending();

            if (showTouch) {
                m_service->getActionCoordinator()->closeTouchNotification();
            }

            onResult(code, validUntil, error);
        }
    });

    m_service->getCredentialService()->generateCodeAsync(m_deviceId, m_credential.originalName);
}

void OathCredentialObject::GenerateCode()
{
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: GenerateCode (async) for credential:"
                              << m_credential.originalName << "on device:" << m_deviceId;

    executeWithCodeGeneration(false, [this](const QString &code, qint64 validUntil, const QString &error) {
        Q_EMIT CodeGenerated(code, validUntil, error);
    });
}

void OathCredentialObject::CopyToClipboard()
{
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: CopyToClipboard (async) for credential:"
                              << m_credential.originalName << "on device:" << m_deviceId;

    auto *copyTimer = new QElapsedTimer();
    copyTimer->start();

    executeWithCodeGeneration(true, [this, copyTimer](const QString &code, qint64 /*validUntil*/, const QString &error) {
        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: [TIMING] CopyToClipboard code generation callback at"
                                  << copyTimer->elapsed() << "ms";

        if (!error.isEmpty()) {
            qCWarning(OathDaemonLog) << "YubiKeyCredentialObject: Code generation failed:" << error;
            delete copyTimer;
            Q_EMIT ClipboardCopied(false, error);
            return;
        }

        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Code generated, copying to clipboard";

        QElapsedTimer actionTimer;
        actionTimer.start();

        auto *device = m_service->getDevice(m_deviceId);
        const Shared::DeviceModel deviceModel = device ? device->deviceModel() : Shared::DeviceModel{};

        auto result = m_service->getActionCoordinator()->executeActionWithNotification(
            code, m_credential.originalName, QStringLiteral("copy"), deviceModel);
        const bool success = (result == ActionExecutor::ActionResult::Success);

        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: [TIMING] CopyToClipboard action took"
                                  << actionTimer.elapsed() << "ms, total:" << copyTimer->elapsed() << "ms";
        delete copyTimer;

        Q_EMIT ClipboardCopied(success, success ? QString() : QStringLiteral("Failed to copy to clipboard"));
    });
}

void OathCredentialObject::TypeCode(bool fallbackToCopy)
{
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: TypeCode (async) for credential:"
                              << m_credential.originalName << "on device:" << m_deviceId
                              << "fallbackToCopy:" << fallbackToCopy;

    auto *typeCodeTimer = new QElapsedTimer();
    typeCodeTimer->start();

    executeWithCodeGeneration(true, [this, fallbackToCopy, typeCodeTimer](const QString &code, qint64 /*validUntil*/, const QString &error) {
        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: [TIMING] Code generation callback at"
                                  << typeCodeTimer->elapsed() << "ms";

        if (!error.isEmpty()) {
            qCWarning(OathDaemonLog) << "YubiKeyCredentialObject: Code generation failed:" << error;
            delete typeCodeTimer;
            Q_EMIT CodeTyped(false, error);
            return;
        }

        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Code generated, typing code";

        QElapsedTimer actionTimer;
        actionTimer.start();

        auto result = m_service->getActionCoordinator()->executeTypeOnly(code, m_credential.originalName);
        bool success = (result == ActionExecutor::ActionResult::Success);

        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: [TIMING] executeTypeOnly took"
                                  << actionTimer.elapsed() << "ms";

        if (!success && fallbackToCopy) {
            qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: TypeCode failed, falling back to clipboard";
            auto *device = m_service->getDevice(m_deviceId);
            const Shared::DeviceModel deviceModel = device ? device->deviceModel() : Shared::DeviceModel{};
            auto copyResult = m_service->getActionCoordinator()->executeActionWithNotification(
                code, m_credential.originalName, QStringLiteral("copy"), deviceModel);
            success = (copyResult == ActionExecutor::ActionResult::Success);
        }

        qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: [TIMING] TypeCode total:"
                                  << typeCodeTimer->elapsed() << "ms";
        delete typeCodeTimer;

        Q_EMIT CodeTyped(success, success ? QString() : QStringLiteral("Failed to type code"));
    });
}

void OathCredentialObject::Delete()
{
    qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Delete (async) credential:"
                              << m_credential.originalName << "from device:" << m_deviceId;

    disconnectPending();

    m_pendingConnection = connect(m_service->getCredentialService(), &CredentialService::credentialDeleted,
            this, [this](const QString &deviceId, const QString &credentialName,
                         bool success, const QString &error) {
        if (deviceId == m_deviceId && credentialName == m_credential.originalName) {
            disconnectPending();

            qCDebug(OathDaemonLog) << "YubiKeyCredentialObject: Credential deleted async:"
                                      << m_credential.originalName << "success:" << success;
            Q_EMIT Deleted(success, error);
        }
    });

    m_service->getCredentialService()->deleteCredentialAsync(m_deviceId, m_credential.originalName);
}

QVariantMap OathCredentialObject::getManagedObjectData() const
{
    QVariantMap result;

    // pl.jkolo.yubikey.oath.Credential interface properties
    QVariantMap credProps;
    credProps.insert(QLatin1String("FullName"), m_credential.originalName);
    credProps.insert(QLatin1String("Issuer"), m_credential.issuer);
    credProps.insert(QLatin1String("Username"), m_credential.account);
    credProps.insert(QLatin1String("RequiresTouch"), m_credential.requiresTouch);
    credProps.insert(QLatin1String("Type"), type());
    credProps.insert(QLatin1String("Algorithm"), algorithm());
    credProps.insert(QLatin1String("Digits"), m_credential.digits);
    credProps.insert(QLatin1String("Period"), m_credential.period);
    credProps.insert(QLatin1String("DeviceId"), m_deviceId);

    result.insert(QLatin1String(CREDENTIAL_INTERFACE), credProps);

    return result;
}

} // namespace Daemon
} // namespace YubiKeyOath
