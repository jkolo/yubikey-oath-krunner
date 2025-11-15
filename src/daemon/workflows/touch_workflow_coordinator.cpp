/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "touch_workflow_coordinator.h"
#include "../actions/yubikey_action_coordinator.h"
#include "notification_orchestrator.h"
#include "config/configuration_provider.h"
#include "../logging_categories.h"
#include "../oath/yubikey_device_manager.h"
#include "../oath/oath_device.h"
#include "../storage/yubikey_database.h"
#include "touch_handler.h"
#include "formatting/credential_formatter.h"
#include "utils/credential_finder.h"
#include "utils/device_name_formatter.h"

#include <KLocalizedString>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

TouchWorkflowCoordinator::TouchWorkflowCoordinator(YubiKeyDeviceManager *deviceManager,
                                                   YubiKeyDatabase *database,
                                                   YubiKeyActionCoordinator *actionCoordinator,
                                                   TouchHandler *touchHandler,
                                                   NotificationOrchestrator *notificationOrchestrator,
                                                   const ConfigurationProvider *config,
                                                   QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_actionCoordinator(actionCoordinator)
    , m_touchHandler(touchHandler)
    , m_notificationOrchestrator(notificationOrchestrator)
    , m_config(config)
    , m_pendingOperationType(OperationType::Generate)  // Initialize to default
{
    init();
}

void TouchWorkflowCoordinator::init()
{
    // Connect touch handler signals
    connect(m_touchHandler, &TouchHandler::touchTimedOut,
            this, &TouchWorkflowCoordinator::onTouchTimeout);
    connect(m_notificationOrchestrator, &NotificationOrchestrator::touchCancelled,
            this, &TouchWorkflowCoordinator::onTouchCancelled);
}

void TouchWorkflowCoordinator::startTouchWorkflow(const QString &credentialName, OperationType operationType, const QString &deviceId, const Shared::DeviceModel& deviceModel)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "Starting touch workflow for:" << credentialName
             << "operation:" << static_cast<int>(operationType) << "device:" << deviceId
             << "brand:" << brandName(deviceModel.brand)
             << "model:" << deviceModel.modelString;

    m_pendingOperationType = operationType;
    m_pendingDeviceId = deviceId;
    m_pendingDeviceModel = deviceModel;
    m_pendingCredentialName = credentialName;
    int const timeout = m_config->touchTimeout();
    m_pendingTouchTimeout = timeout;
    qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout from config:" << timeout << "seconds";

    // Emit signal for D-Bus clients (can show custom notification)
    Q_EMIT touchRequired(timeout, deviceModel.modelString);

    // Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeout);

    // NOTE: Notification will be shown when device emits touchRequired() signal
    // (after CALCULATE APDU is sent and device LED starts flashing)

    // Get device and connect to its touchRequired signal for delayed notification
    auto *device = m_deviceManager->getDeviceOrFirst(deviceId);
    if (device) {
        // Disconnect any previous connection
        if (m_deviceConnection) {
            QObject::disconnect(m_deviceConnection);
        }

        // Connect to device touchRequired signal (will be emitted when CALCULATE APDU sent)
        m_deviceConnection = connect(device, &OathDevice::touchRequired,
                                     this, &TouchWorkflowCoordinator::onDeviceTouchDetected,
                                     Qt::UniqueConnection);

        qCDebug(TouchWorkflowCoordinatorLog) << "Connected to device touchRequired signal for delayed notification";
    } else {
        qCWarning(TouchWorkflowCoordinatorLog) << "Device not found for touch signal connection:" << deviceId;
    }

    // Start asynchronous code generation via DeviceManager
    qCDebug(TouchWorkflowCoordinatorLog) << "Starting async code generation for:" << credentialName << "device:" << deviceId;

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, credentialName]() {
        QString const code = watcher->result();
        if (!code.isEmpty()) {
            onCodeGenerated(credentialName, code);
        } else {
            onCodeGenerationFailed(credentialName, i18n("Failed to generate code"));
        }
        watcher->deleteLater();
    });

    QFuture<QString> const future = QtConcurrent::run([this, deviceId, credentialName]() -> QString {
        auto *device = m_deviceManager->getDeviceOrFirst(deviceId);
        if (!device) {
            qCWarning(TouchWorkflowCoordinatorLog) << "Device not found:" << deviceId;
            return {};
        }

        auto result = device->generateCode(credentialName);
        if (result.isSuccess()) {
            return result.value();
        } else {
            qCWarning(TouchWorkflowCoordinatorLog) << "Code generation failed:" << result.error();
            return {};
        }
    });
    watcher->setFuture(future);
}

void TouchWorkflowCoordinator::onCodeGenerated(const QString &credentialName, const QString &code)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "onCodeGenerated() called for:" << credentialName
             << "code length:" << code.length();

    // Verify this is for the credential we're waiting for
    QString const waitingFor = m_touchHandler->waitingCredential();
    if (waitingFor != credentialName) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Ignoring code for" << credentialName
                 << "- waiting for:" << waitingFor;
        return;
    }

    qCDebug(TouchWorkflowCoordinatorLog) << "Touch successful, executing pending action";

    // Emit signal for D-Bus clients
    Q_EMIT touchCompleted(true);

    // Close touch notification
    m_notificationOrchestrator->closeTouchNotification();

    // Stop timers
    m_touchHandler->cancelTouchOperation();

    // Find credential and format display name according to configuration
    QList<OathCredential> const credentials = m_deviceManager->getCredentials();

    // Find credential using shared utility function
    auto foundCredentialOpt = Utils::findCredential(credentials, credentialName, m_pendingDeviceId);

    // Format credential display name according to configuration (same as KRunner)
    QString formattedTitle = credentialName; // fallback to raw name
    if (foundCredentialOpt.has_value()) {
        const OathCredential &foundCredential = foundCredentialOpt.value();
        const QString deviceName = DeviceNameFormatter::getDeviceDisplayName(m_pendingDeviceId, m_database);

        const int connectedDeviceCount = static_cast<int>(m_deviceManager->getConnectedDeviceIds().size());

        const FormatOptions options(
            m_config->showUsername(),
            false, // Don't show code in title (code is shown in notification body)
            m_config->showDeviceName(),
            deviceName,
            connectedDeviceCount,
            m_config->showDeviceNameOnlyWhenMultiple()
        );

        formattedTitle = CredentialFormatter::formatDisplayName(foundCredential, options);
    } else {
        qCWarning(TouchWorkflowCoordinatorLog) << "Credential not found for formatting:" << credentialName;
    }

    // Map OperationType to actionId string for ActionCoordinator
    QString actionId;
    switch (m_pendingOperationType) {
    case OperationType::Generate:
        actionId = QStringLiteral("generate");
        break;
    case OperationType::Copy:
        actionId = QStringLiteral("copy");
        break;
    case OperationType::Type:
        actionId = QStringLiteral("type");
        break;
    case OperationType::Delete:
        actionId = QStringLiteral("delete");
        break;
    }
    qCDebug(TouchWorkflowCoordinatorLog) << "Executing action after touch:" << actionId;

    // Use YubiKeyActionCoordinator's unified executeActionWithNotification() method
    // This ensures consistent notification policy with non-touch path:
    // - Copy action: always show notification on success
    // - Type action: never show code notification (user sees code being typed)
    // - Generate action: show code notification
    m_actionCoordinator->executeActionWithNotification(code, formattedTitle, actionId, m_pendingDeviceModel);

    // Clear pending operation and device
    m_pendingOperationType = OperationType::Copy; // Reset to default
    m_pendingDeviceId.clear();
    qCDebug(TouchWorkflowCoordinatorLog) << "Touch handling completed successfully";
}

void TouchWorkflowCoordinator::onCodeGenerationFailed(const QString &credentialName, const QString &error)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "onCodeGenerationFailed() called for:" << credentialName
             << "error:" << error;

    // Verify this is for the credential we're waiting for
    QString const waitingFor = m_touchHandler->waitingCredential();
    if (waitingFor != credentialName) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Ignoring failure for" << credentialName
                 << "- waiting for:" << waitingFor;
        return;
    }

    qCDebug(TouchWorkflowCoordinatorLog) << "Code generation failed, cleaning up";

    // Emit signal for D-Bus clients
    Q_EMIT touchCompleted(false);

    // Clean up
    cleanupTouchWorkflow();
}

void TouchWorkflowCoordinator::onTouchTimeout(const QString &credentialName)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "onTouchTimeout() called for:" << credentialName << "device:" << m_pendingDeviceId;

    if (!credentialName.isEmpty()) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout";

        // Emit signal for D-Bus clients
        Q_EMIT touchCompleted(false);

        // Note: D-Bus operations can't be cancelled, but the timeout will be
        // handled by ignoring the result if it arrives after timeout

        // Clean up workflow state
        cleanupTouchWorkflow();

        qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout handled";
    }
}

void TouchWorkflowCoordinator::onTouchCancelled()
{
    qCDebug(TouchWorkflowCoordinatorLog) << "Touch operation cancelled by user";

    // Emit signal for D-Bus clients
    Q_EMIT touchCompleted(false);

    QString const credentialName = m_touchHandler->waitingCredential();
    cleanupTouchWorkflow();

    m_notificationOrchestrator->showSimpleNotification(
        i18n("Cancelled"),
        i18n("Touch operation cancelled for '%1'", credentialName),
        0);
}

void TouchWorkflowCoordinator::onDeviceTouchDetected()
{
    qCDebug(TouchWorkflowCoordinatorLog) << "Device touchRequired signal detected - LED is now flashing"
             << "credential:" << m_pendingCredentialName
             << "timeout:" << m_pendingTouchTimeout
             << "device:" << m_pendingDeviceModel.modelString;

    // Verify touch operation is still active (not timed out or cancelled)
    QString const waitingFor = m_touchHandler->waitingCredential();
    if (waitingFor.isEmpty()) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Touch operation no longer active - ignoring signal";
        return;
    }

    if (waitingFor != m_pendingCredentialName) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Touch signal for different credential - ignoring"
                 << "waiting for:" << waitingFor << "signal for:" << m_pendingCredentialName;
        return;
    }

    // NOW show the notification - device LED is actually flashing
    m_notificationOrchestrator->showTouchNotification(
        m_pendingCredentialName,
        m_pendingTouchTimeout,
        m_pendingDeviceModel);

    qCDebug(TouchWorkflowCoordinatorLog) << "Touch notification shown (synchronized with LED)";

    // Disconnect signal - we only need it once per workflow
    if (m_deviceConnection) {
        QObject::disconnect(m_deviceConnection);
        m_deviceConnection = {};
    }
}

void TouchWorkflowCoordinator::cleanupTouchWorkflow()
{
    m_touchHandler->cancelTouchOperation();
    m_notificationOrchestrator->closeTouchNotification();
    m_pendingOperationType = OperationType::Copy; // Reset to default
    m_pendingDeviceId.clear();
    m_pendingDeviceModel = Shared::DeviceModel{};
    m_pendingCredentialName.clear();

    // Disconnect device touchRequired signal if still connected
    if (m_deviceConnection) {
        QObject::disconnect(m_deviceConnection);
        m_deviceConnection = {};
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
