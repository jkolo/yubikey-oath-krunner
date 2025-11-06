/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "reconnect_workflow_coordinator.h"
#include "../actions/yubikey_action_coordinator.h"
#include "../services/yubikey_service.h"
#include "notification_orchestrator.h"
#include "config/configuration_provider.h"
#include "../logging_categories.h"
#include "../storage/yubikey_database.h"
#include "utils/device_name_formatter.h"

#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

ReconnectWorkflowCoordinator::ReconnectWorkflowCoordinator(YubiKeyService *service,
                                                           YubiKeyDatabase *database,
                                                           YubiKeyActionCoordinator *actionCoordinator,
                                                           NotificationOrchestrator *notificationOrchestrator,
                                                           const ConfigurationProvider *config,
                                                           QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_database(database)
    , m_actionCoordinator(actionCoordinator)
    , m_notificationOrchestrator(notificationOrchestrator)
    , m_config(config)
    , m_timeoutTimer(new QTimer(this))
{
    init();
}

void ReconnectWorkflowCoordinator::init()
{
    // Connect authentication success signal - emitted when device connected and authenticated successfully
    // This is the definitive "ready" signal - device is connected with valid credentials
    connect(m_service, &YubiKeyService::deviceConnectedAndAuthenticated,
            this, &ReconnectWorkflowCoordinator::onDeviceAuthenticationSuccess);

    // NOTE: We DO NOT connect to deviceConnectedAuthenticationFailed for reconnect workflow!
    // Reason: During reconnect, YubiKeyService may emit this signal during first attempt
    // before it retries with password from KWallet. If we react too early, we show
    // "Wrong password" error when authentication is still in progress.
    // Instead, we rely on:
    // 1. deviceConnectedAndAuthenticated - success case
    // 2. timeout - failure case (shows timeout message, not "wrong password")

    // Connect notification cancel signal
    connect(m_notificationOrchestrator, &NotificationOrchestrator::reconnectCancelled,
            this, &ReconnectWorkflowCoordinator::onReconnectCancelled);

    // Setup timeout timer
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout,
            this, &ReconnectWorkflowCoordinator::onReconnectTimeout);
}

void ReconnectWorkflowCoordinator::startReconnectWorkflow(const QString &deviceId,
                                                          const QString &credentialName,
                                                          const QString &actionId)
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Starting reconnect workflow"
                              << "device:" << deviceId
                              << "credential:" << credentialName
                              << "action:" << actionId;

    // Cancel previous workflow if any
    if (m_waitingForReconnect) {
        qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Cancelling previous workflow";
        cleanupReconnectWorkflow();
    }

    // Store workflow state
    m_waitingForReconnect = true;
    m_pendingDeviceId = deviceId;
    m_pendingCredentialName = credentialName;
    m_pendingActionId = actionId;

    // Get device name for notification
    QString const deviceName = DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);

    // Get timeout from configuration
    int const timeoutSeconds = m_config->deviceReconnectTimeout();
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout:" << timeoutSeconds << "seconds";

    // Show reconnect notification
    m_notificationOrchestrator->showReconnectNotification(deviceName, credentialName, timeoutSeconds);

    // Start timeout timer
    m_timeoutTimer->start(timeoutSeconds * 1000);

    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Waiting for device reconnection";
}

void ReconnectWorkflowCoordinator::onDeviceAuthenticationSuccess(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Device authentication success"
                              << "deviceId:" << deviceId
                              << "waiting for:" << m_pendingDeviceId;

    // Only handle if we're waiting for this specific device
    if (!m_waitingForReconnect || deviceId != m_pendingDeviceId) {
        return;
    }

    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Processing reconnect for device:" << deviceId;

    // Stop timeout timer and close reconnect notification
    m_timeoutTimer->stop();
    m_notificationOrchestrator->closeReconnectNotification();

    // Store action ID for cleanup (executeActionInternal is async for touch)
    QString const actionId = m_pendingActionId.isEmpty() ? QStringLiteral("copy") : m_pendingActionId;

    // Delegate to ActionCoordinator which handles:
    // - Device lookup and validation
    // - Credential lookup
    // - Touch workflow coordination (async) OR direct code generation
    // - Action execution with notifications
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Delegating to ActionCoordinator for device:"
                              << deviceId << "credential:" << m_pendingCredentialName << "action:" << actionId;

    bool const success = m_actionCoordinator->executeActionInternal(
        m_pendingDeviceId,
        m_pendingCredentialName,
        actionId
    );

    if (!success) {
        qCWarning(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: ActionCoordinator failed to execute action";
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Failed to execute action after reconnect"),
            0);
    } else {
        qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: ActionCoordinator executing action (may be async for touch)";
    }

    // Cleanup workflow state
    cleanupReconnectWorkflow();
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Workflow completed";
}

void ReconnectWorkflowCoordinator::onReconnectTimeout()
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout"
                              << "device:" << m_pendingDeviceId
                              << "credential:" << m_pendingCredentialName;

    // Close reconnect notification
    m_notificationOrchestrator->closeReconnectNotification();

    // Show timeout notification
    QString const deviceName = DeviceNameFormatter::getDeviceDisplayName(m_pendingDeviceId, m_database);
    m_notificationOrchestrator->showSimpleNotification(
        i18n("Timeout"),
        i18n("YubiKey '%1' was not reconnected in time", deviceName),
        0);

    // Cleanup
    cleanupReconnectWorkflow();
}

void ReconnectWorkflowCoordinator::onReconnectCancelled()
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect cancelled by user";

    // Show cancellation notification
    QString const deviceName = DeviceNameFormatter::getDeviceDisplayName(m_pendingDeviceId, m_database);
    m_notificationOrchestrator->showSimpleNotification(
        i18n("Cancelled"),
        i18n("Reconnect to '%1' cancelled", deviceName),
        0);

    // Cleanup
    cleanupReconnectWorkflow();
}

void ReconnectWorkflowCoordinator::cleanupReconnectWorkflow()
{
    m_waitingForReconnect = false;
    m_timeoutTimer->stop();
    m_pendingDeviceId.clear();
    m_pendingCredentialName.clear();
    m_pendingActionId.clear();
}

} // namespace Daemon
} // namespace YubiKeyOath
