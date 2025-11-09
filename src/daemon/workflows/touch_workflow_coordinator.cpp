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

void TouchWorkflowCoordinator::startTouchWorkflow(const QString &credentialName, const QString &actionId, const QString &deviceId, Shared::YubiKeyModel deviceModel)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "Starting touch workflow for:" << credentialName
             << "action:" << actionId << "device:" << deviceId
             << "deviceModel:" << QString::number(deviceModel, 16);

    m_pendingActionId = actionId;
    m_pendingDeviceId = deviceId;
    m_pendingDeviceModel = deviceModel;
    int const timeout = m_config->touchTimeout();
    qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout from config:" << timeout << "seconds";

    // Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeout);

    // Show touch notification with device model
    m_notificationOrchestrator->showTouchNotification(credentialName, timeout, deviceModel);

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

    // Execute the pending action using unified notification policy
    QString const actionId = m_pendingActionId.isEmpty() ? QStringLiteral("copy") : m_pendingActionId;
    qCDebug(TouchWorkflowCoordinatorLog) << "Executing action after touch:" << actionId;

    // Use YubiKeyActionCoordinator's unified executeActionWithNotification() method
    // This ensures consistent notification policy with non-touch path:
    // - Copy action: always show notification on success
    // - Type action: never show code notification (user sees code being typed)
    m_actionCoordinator->executeActionWithNotification(code, formattedTitle, actionId, m_pendingDeviceModel);

    // Clear pending action and device
    m_pendingActionId.clear();
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

    // Clean up
    cleanupTouchWorkflow();
}

void TouchWorkflowCoordinator::onTouchTimeout(const QString &credentialName)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "onTouchTimeout() called for:" << credentialName << "device:" << m_pendingDeviceId;

    if (!credentialName.isEmpty()) {
        qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout";

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

    QString const credentialName = m_touchHandler->waitingCredential();
    cleanupTouchWorkflow();

    m_notificationOrchestrator->showSimpleNotification(
        i18n("Cancelled"),
        i18n("Touch operation cancelled for '%1'", credentialName),
        0);
}

void TouchWorkflowCoordinator::cleanupTouchWorkflow()
{
    m_touchHandler->cancelTouchOperation();
    m_notificationOrchestrator->closeTouchNotification();
    m_pendingActionId.clear();
    m_pendingDeviceId.clear();
    m_pendingDeviceModel = 0;
}

} // namespace Daemon
} // namespace YubiKeyOath
