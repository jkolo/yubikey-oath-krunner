/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "reconnect_workflow_coordinator.h"
#include "../actions/yubikey_action_coordinator.h"
#include "notification_orchestrator.h"
#include "config/configuration_provider.h"
#include "interfaces/credential_update_notifier.h"
#include "../logging_categories.h"
#include "../oath/yubikey_device_manager.h"
#include "../storage/yubikey_database.h"
#include "formatting/credential_formatter.h"
#include "utils/device_name_formatter.h"

#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

ReconnectWorkflowCoordinator::ReconnectWorkflowCoordinator(ICredentialUpdateNotifier *notifier,
                                                           YubiKeyDatabase *database,
                                                           YubiKeyActionCoordinator *actionCoordinator,
                                                           NotificationOrchestrator *notificationOrchestrator,
                                                           const ConfigurationProvider *config,
                                                           QObject *parent)
    : QObject(parent)
    , m_notifier(notifier)
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
    // Connect credentialsUpdated signal from notifier (emitted AFTER password is loaded)
    // This ensures device has valid password when we try to generate code
    connect(m_notifier, &ICredentialUpdateNotifier::credentialsUpdated,
            this, &ReconnectWorkflowCoordinator::onDeviceConnected);

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
    QString const deviceName = getDeviceName(deviceId);

    // Get timeout from configuration
    int const timeoutSeconds = m_config->deviceReconnectTimeout();
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout:" << timeoutSeconds << "seconds";

    // Show reconnect notification
    m_notificationOrchestrator->showReconnectNotification(deviceName, credentialName, timeoutSeconds);

    // Start timeout timer
    m_timeoutTimer->start(timeoutSeconds * 1000);

    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Waiting for device reconnection";
}

void ReconnectWorkflowCoordinator::onDeviceConnected(const QString &deviceId)
{
    // Only handle if we're waiting for reconnect and this is the right device
    if (!m_waitingForReconnect || deviceId != m_pendingDeviceId) {
        return;
    }

    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Device reconnected:" << deviceId;

    // Stop timeout timer and close reconnect notification
    m_timeoutTimer->stop();
    m_notificationOrchestrator->closeReconnectNotification();

    // Get device instance
    auto *device = m_notifier->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Device not found after reconnect:" << deviceId;
        cleanupReconnectWorkflow();
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Device reconnected but is no longer available"),
            0);
        return;
    }

    // Validate authentication before attempting code generation
    bool const requiresPassword = m_database->requiresPassword(deviceId);
    if (requiresPassword && !device->hasPassword()) {
        qCWarning(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Device requires authentication but password is not loaded";
        cleanupReconnectWorkflow();
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Authentication required. Please unlock the device first."),
            0);
        return;
    }

    // Find credential
    auto credential = findCredentialForReconnect();
    if (!credential) {
        qCWarning(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Credential not found";
        cleanupReconnectWorkflow();
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Credential not found"),
            0);
        return;
    }

    // Show touch notification if needed
    showTouchNotificationIfRequired(*credential);

    // Generate code
    auto codeResult = generateCodeWithNotifications(device, m_pendingCredentialName);
    if (codeResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Failed to generate code:" << codeResult.error();
        cleanupReconnectWorkflow();
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Failed to generate code: %1", codeResult.error()),
            0);
        return;
    }

    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Code generated successfully";

    // Format credential title and execute action
    QString const title = formatCredentialTitle(*credential);
    executeActionAfterReconnect(codeResult.value(), title);

    // Cleanup
    cleanupReconnectWorkflow();
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Workflow completed successfully";
}

void ReconnectWorkflowCoordinator::onReconnectTimeout()
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout"
                              << "device:" << m_pendingDeviceId
                              << "credential:" << m_pendingCredentialName;

    // Close reconnect notification
    m_notificationOrchestrator->closeReconnectNotification();

    // Show timeout notification
    QString const deviceName = getDeviceName(m_pendingDeviceId);
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
    QString const deviceName = getDeviceName(m_pendingDeviceId);
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
    m_touchNotificationShown = false;
    m_timeoutTimer->stop();
    m_pendingDeviceId.clear();
    m_pendingCredentialName.clear();
    m_pendingActionId.clear();
}

std::optional<OathCredential> ReconnectWorkflowCoordinator::findCredentialForReconnect()
{
    QList<OathCredential> const credentials = m_notifier->getCredentials();
    for (const auto &cred : credentials) {
        if (cred.originalName == m_pendingCredentialName && cred.deviceId == m_pendingDeviceId) {
            return cred;
        }
    }
    return std::nullopt;
}

void ReconnectWorkflowCoordinator::showTouchNotificationIfRequired(const OathCredential &credential)
{
    if (!credential.requiresTouch) {
        return;
    }

    int const touchTimeout = m_config->touchTimeout();
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Credential requires touch, showing notification"
                              << "timeout:" << touchTimeout << "seconds";
    m_notificationOrchestrator->showTouchNotification(m_pendingCredentialName, touchTimeout);
    m_touchNotificationShown = true;
}

Result<QString> ReconnectWorkflowCoordinator::generateCodeWithNotifications(
    YubiKeyOathDevice *device,
    const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Generating code for:" << credentialName;
    auto result = device->generateCode(credentialName);

    // Close touch notification if it was shown
    if (m_touchNotificationShown) {
        qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Closing touch notification";
        m_notificationOrchestrator->closeTouchNotification();
        m_touchNotificationShown = false;
    }

    return result;
}

QString ReconnectWorkflowCoordinator::formatCredentialTitle(const OathCredential &credential)
{
    QString const deviceName = getDeviceName(m_pendingDeviceId);
    int const connectedDeviceCount = m_notifier->getConnectedDeviceIds().size();

    FormatOptions options = FormatOptionsBuilder()
        .withUsername(m_config->showUsername())
        .withCode(false) // Don't show code in title
        .withDevice(deviceName, m_config->showDeviceName())
        .withDeviceCount(connectedDeviceCount)
        .onlyWhenMultipleDevices(m_config->showDeviceNameOnlyWhenMultiple())
        .build();

    return CredentialFormatter::formatDisplayName(credential, options);
}

void ReconnectWorkflowCoordinator::executeActionAfterReconnect(const QString &code, const QString &title)
{
    QString const actionId = m_pendingActionId.isEmpty() ? QStringLiteral("copy") : m_pendingActionId;
    qCDebug(YubiKeyDaemonLog) << "ReconnectWorkflowCoordinator: Executing action after reconnect:" << actionId;
    m_actionCoordinator->executeActionWithNotification(code, title, actionId);
}

QString ReconnectWorkflowCoordinator::getDeviceName(const QString &deviceId) const
{
    // Delegate to DeviceNameFormatter for consistent name handling
    return DeviceNameFormatter::getDeviceDisplayName(deviceId, m_database);
}

} // namespace Daemon
} // namespace YubiKeyOath
