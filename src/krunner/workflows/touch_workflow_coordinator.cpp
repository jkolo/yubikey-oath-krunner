/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "touch_workflow_coordinator.h"
#include "../actions/action_executor.h"
#include "notification_orchestrator.h"
#include "notification_helper.h"
#include "../config/configuration_provider.h"
#include "../logging_categories.h"
#include "../shared/dbus/yubikey_dbus_client.h"
#include "workflows/touch_handler.h"

#include <KLocalizedString>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

TouchWorkflowCoordinator::TouchWorkflowCoordinator(YubiKeyDBusClient *dbusClient,
                                                   TouchHandler *touchHandler,
                                                   ActionExecutor *actionExecutor,
                                                   NotificationOrchestrator *notificationOrchestrator,
                                                   const ConfigurationProvider *config,
                                                   QObject *parent)
    : QObject(parent)
    , m_dbusClient(dbusClient)
    , m_touchHandler(touchHandler)
    , m_actionExecutor(actionExecutor)
    , m_notificationOrchestrator(notificationOrchestrator)
    , m_config(config)
{
    // Connect touch handler signals
    connect(m_touchHandler, &TouchHandler::touchTimedOut,
            this, &TouchWorkflowCoordinator::onTouchTimeout);
    connect(m_notificationOrchestrator, &NotificationOrchestrator::touchCancelled,
            this, &TouchWorkflowCoordinator::onTouchCancelled);
}

void TouchWorkflowCoordinator::startTouchWorkflow(const QString &credentialName, const QString &actionId, const QString &deviceId)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "Starting touch workflow for:" << credentialName
             << "action:" << actionId << "device:" << deviceId;

    m_pendingActionId = actionId;
    m_pendingDeviceId = deviceId;
    int timeout = m_config->touchTimeout();
    qCDebug(TouchWorkflowCoordinatorLog) << "Touch timeout from config:" << timeout << "seconds";

    // Start touch operation
    m_touchHandler->startTouchOperation(credentialName, timeout);

    // Show touch notification
    m_notificationOrchestrator->showTouchNotification(credentialName, timeout);

    // Start asynchronous code generation via QtConcurrent
    qCDebug(TouchWorkflowCoordinatorLog) << "Starting async code generation via D-Bus for:" << credentialName << "device:" << deviceId;

    // Use QtConcurrent to run D-Bus call in background thread
    auto *watcher = new QFutureWatcher<GenerateCodeResult>(this);
    connect(watcher, &QFutureWatcher<GenerateCodeResult>::finished, this, [this, watcher, credentialName]() {
        GenerateCodeResult result = watcher->result();
        if (!result.code.isEmpty()) {
            onCodeGenerated(credentialName, result.code);
        } else {
            onCodeGenerationFailed(credentialName, i18n("Failed to generate code"));
        }
        watcher->deleteLater();
    });

    QFuture<GenerateCodeResult> future = QtConcurrent::run([this, deviceId, credentialName]() {
        return m_dbusClient->generateCode(deviceId, credentialName);
    });
    watcher->setFuture(future);
}

void TouchWorkflowCoordinator::onCodeGenerated(const QString &credentialName, const QString &code)
{
    qCDebug(TouchWorkflowCoordinatorLog) << "onCodeGenerated() called for:" << credentialName
             << "code length:" << code.length();

    // Verify this is for the credential we're waiting for
    QString waitingFor = m_touchHandler->waitingCredential();
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

    // Execute the pending action
    QString actionId = m_pendingActionId.isEmpty() ? QStringLiteral("copy") : m_pendingActionId;
    qCDebug(TouchWorkflowCoordinatorLog) << "Executing action after touch:" << actionId;

    if (actionId == QStringLiteral("type")) {
        auto result = m_actionExecutor->executeTypeAction(code, credentialName);

        // If permission was rejected, code was copied to clipboard as fallback
        // Show code notification (ActionExecutor already showed "Permission Denied" notification)
        if (result == ActionExecutor::ActionResult::Failed) {
            int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);
            m_notificationOrchestrator->showCodeNotification(code, credentialName, totalSeconds);
        }
    } else {
        auto result = m_actionExecutor->executeCopyAction(code, credentialName);

        // Show code notification for copy action
        if (result == ActionExecutor::ActionResult::Success) {
            int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);
            m_notificationOrchestrator->showCodeNotification(code, credentialName, totalSeconds);
        }
    }

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
    QString waitingFor = m_touchHandler->waitingCredential();
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

    QString credentialName = m_touchHandler->waitingCredential();
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
}

} // namespace YubiKey
} // namespace KRunner
