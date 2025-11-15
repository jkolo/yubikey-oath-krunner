/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "reconnect_workflow_coordinator.h"
#include "../actions/oath_action_coordinator.h"
#include "../services/oath_service.h"
#include "notification_orchestrator.h"
#include "config/configuration_provider.h"
#include "../logging_categories.h"
#include "../storage/oath_database.h"
#include "utils/device_name_formatter.h"
#include "shared/types/device_brand.h"
#include "shared/types/yubikey_model.h"

#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

ReconnectWorkflowCoordinator::ReconnectWorkflowCoordinator(OathService *service,
                                                           OathDatabase *database,
                                                           OathActionCoordinator *actionCoordinator,
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
    connect(m_service, &OathService::deviceConnectedAndAuthenticated,
            this, &ReconnectWorkflowCoordinator::onDeviceAuthenticationSuccess);

    // NOTE: We DO NOT connect to deviceConnectedAuthenticationFailed for reconnect workflow!
    // Reason: During reconnect, OathService may emit this signal during first attempt
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

/**
 * @brief Reconstructs DeviceModel from database for offline devices
 *
 * When a device is disconnected, we can't query it directly for model information.
 * This helper reconstructs DeviceModel from cached database data using:
 * - deviceName to detect brand (YubiKey/Nitrokey via pattern matching)
 * - deviceModel (YubiKeyModel uint32_t) to get full model details
 *
 * This allows showing device-specific icons even when device is offline.
 *
 * @param deviceId Device ID to look up in database
 * @return DeviceModel reconstructed from database, or empty if device not found
 */
Shared::DeviceModel ReconnectWorkflowCoordinator::deviceModelFromDatabase(const QString &deviceId)
{
    // Try to get device from database
    std::optional<OathDatabase::DeviceRecord> const record = m_database->getDevice(deviceId);

    if (!record.has_value()) {
        qCWarning(OathDaemonLog) << "ReconnectWorkflowCoordinator: Device not found in database:" << deviceId;
        return Shared::DeviceModel{}; // Return empty model
    }

    // Detect brand from device name (user-friendly name like "YubiKey 5C NFC" or "Nitrokey 3C")
    Shared::DeviceBrand const brand = Shared::detectBrandFromModelString(record->deviceName);

    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Reconstructing DeviceModel from database"
                              << "deviceName:" << record->deviceName
                              << "brand:" << Shared::brandName(brand)
                              << "modelCode:" << QString(QStringLiteral("0x%1")).arg(record->deviceModel, 8, 16, QLatin1Char('0'));

    // Validate brand detection - warn if we have modelCode but couldn't detect brand
    if (brand == Shared::DeviceBrand::Unknown && record->deviceModel != 0) {
        qCWarning(OathDaemonLog) << "ReconnectWorkflowCoordinator: Could not detect brand from device name"
                                    << record->deviceName
                                    << "but have valid modelCode"
                                    << QString(QStringLiteral("0x%1")).arg(record->deviceModel, 8, 16, QLatin1Char('0'))
                                    << "- device will use generic fallback icon";
    }

    // Reconstruct DeviceModel based on brand
    if (brand == Shared::DeviceBrand::YubiKey) {
        // Use built-in YubiKey conversion function
        Shared::DeviceModel model = Shared::toDeviceModel(record->deviceModel);
        qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: YubiKey model:" << model.modelString;
        return model;
    }

    // For Nitrokey (or Unknown), construct basic DeviceModel manually
    // We don't have full Nitrokey model decoder available here, but we can provide basic info
    Shared::DeviceModel model;
    model.brand = brand;
    model.modelCode = record->deviceModel;

    // Use device name as model string (best we can do without full decoder)
    // This works because deviceName is typically auto-generated as "Nitrokey 3C NFC" etc.
    model.modelString = record->deviceName;

    // Basic capabilities for Nitrokey 3 (all variants support these)
    if (brand == Shared::DeviceBrand::Nitrokey) {
        model.capabilities = {
            QStringLiteral("FIDO2"),
            QStringLiteral("OATH-HOTP"),
            QStringLiteral("OATH-TOTP"),
            QStringLiteral("OpenPGP"),
            QStringLiteral("PIV"),
        };
    }

    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Reconstructed model:" << model.modelString;
    return model;
}

void ReconnectWorkflowCoordinator::startReconnectWorkflow(const QString &deviceId,
                                                          const QString &credentialName,
                                                          const QString &actionId)
{
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Starting reconnect workflow"
                              << "device:" << deviceId
                              << "credential:" << credentialName
                              << "action:" << actionId;

    // Cancel previous workflow if any
    if (m_waitingForReconnect) {
        qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Cancelling previous workflow";
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
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout:" << timeoutSeconds << "seconds";

    // Reconstruct DeviceModel from database to show device-specific icon even when offline
    // This uses cached device information (name, model code) to display the correct icon
    const Shared::DeviceModel deviceModel = deviceModelFromDatabase(deviceId);

    // Emit signal for D-Bus clients (can show custom notification)
    Q_EMIT reconnectRequired(deviceModel.modelString);

    m_notificationOrchestrator->showReconnectNotification(deviceName, credentialName, timeoutSeconds, deviceModel);

    // Start timeout timer
    m_timeoutTimer->start(timeoutSeconds * 1000);

    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Waiting for device reconnection";
}

void ReconnectWorkflowCoordinator::onDeviceAuthenticationSuccess(const QString &deviceId)
{
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Device authentication success"
                              << "deviceId:" << deviceId
                              << "waiting for:" << m_pendingDeviceId;

    // Only handle if we're waiting for this specific device
    if (!m_waitingForReconnect || deviceId != m_pendingDeviceId) {
        return;
    }

    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Processing reconnect for device:" << deviceId;

    // Emit signal for D-Bus clients
    Q_EMIT reconnectCompleted(true);

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
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Delegating to ActionCoordinator for device:"
                              << deviceId << "credential:" << m_pendingCredentialName << "action:" << actionId;

    bool const success = m_actionCoordinator->executeActionInternal(
        m_pendingDeviceId,
        m_pendingCredentialName,
        actionId
    );

    if (!success) {
        qCWarning(OathDaemonLog) << "ReconnectWorkflowCoordinator: ActionCoordinator failed to execute action";
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Error"),
            i18n("Failed to execute action after reconnect"),
            0);
    } else {
        qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: ActionCoordinator executing action (may be async for touch)";
    }

    // Cleanup workflow state
    cleanupReconnectWorkflow();
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Workflow completed";
}

void ReconnectWorkflowCoordinator::onReconnectTimeout()
{
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect timeout"
                              << "device:" << m_pendingDeviceId
                              << "credential:" << m_pendingCredentialName;

    // Emit signal for D-Bus clients
    Q_EMIT reconnectCompleted(false);

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
    qCDebug(OathDaemonLog) << "ReconnectWorkflowCoordinator: Reconnect cancelled by user";

    // Emit signal for D-Bus clients
    Q_EMIT reconnectCompleted(false);

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
