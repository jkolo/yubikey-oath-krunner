/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_action_coordinator.h"
#include "action_executor.h"
#include "../clipboard/clipboard_manager.h"
#include "../input/text_input_factory.h"
#include "../notification/dbus_notification_manager.h"
#include "../workflows/notification_orchestrator.h"
#include "../workflows/notification_helper.h"
#include "../workflows/touch_handler.h"
#include "../workflows/touch_workflow_coordinator.h"
#include "../oath/yubikey_device_manager.h"
#include "../storage/yubikey_database.h"
#include "../config/daemon_configuration.h"
#include "../logging_categories.h"
#include "formatting/credential_formatter.h"

#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

YubiKeyActionCoordinator::YubiKeyActionCoordinator(YubiKeyDeviceManager *deviceManager,
                                         YubiKeyDatabase *database,
                                         DaemonConfiguration *config,
                                         QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_config(config)
    , m_clipboardManager(std::make_unique<ClipboardManager>(this))
    , m_textInput(TextInputFactory::createProvider(this))
    , m_notificationManager(std::make_unique<DBusNotificationManager>(this))
    , m_notificationOrchestrator(std::make_unique<NotificationOrchestrator>(
        m_notificationManager.get(),
        config,
        this))
    , m_actionExecutor(std::make_unique<ActionExecutor>(
        m_textInput.get(),
        m_clipboardManager.get(),
        config,
        m_notificationOrchestrator.get(),
        this))
    , m_touchHandler(std::make_unique<TouchHandler>(this))
    , m_touchWorkflowCoordinator(std::make_unique<TouchWorkflowCoordinator>(
        deviceManager,
        database,
        this, // Pass action coordinator for unified action execution
        m_touchHandler.get(),
        m_notificationOrchestrator.get(),
        config,
        this))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyActionCoordinator: Initialized with touch workflow support";
}

YubiKeyActionCoordinator::~YubiKeyActionCoordinator() = default;

bool YubiKeyActionCoordinator::copyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: copyCodeToClipboard" << credentialName;
    return executeActionInternal(deviceId, credentialName, QStringLiteral("copy"));
}

bool YubiKeyActionCoordinator::typeCode(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: typeCode" << credentialName;
    return executeActionInternal(deviceId, credentialName, QStringLiteral("type"));
}

ActionExecutor::ActionResult YubiKeyActionCoordinator::executeActionWithNotification(const QString &code,
                                                                                    const QString &credentialName,
                                                                                    const QString &actionType)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: executeActionWithNotification"
                                << "action:" << actionType << "credential:" << credentialName;

    // Execute action based on type
    ActionExecutor::ActionResult result;
    if (actionType == QStringLiteral("copy")) {
        result = m_actionExecutor->executeCopyAction(code, credentialName);

        // Copy action: always show notification on success
        if (result == ActionExecutor::ActionResult::Success) {
            int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);
            m_notificationOrchestrator->showCodeNotification(code, credentialName, totalSeconds);
        }
    } else if (actionType == QStringLiteral("type")) {
        result = m_actionExecutor->executeTypeAction(code, credentialName);

        // Type action: never show code notification (user sees code being typed)
        // Error notifications are already handled by ActionExecutor
    } else {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Unknown action type:" << actionType;
        return ActionExecutor::ActionResult::Failed;
    }

    return result;
}

bool YubiKeyActionCoordinator::executeActionInternal(const QString &deviceId,
                                                     const QString &credentialName,
                                                     const QString &actionType)
{
    // Get device (use first connected if deviceId empty)
    auto device = m_deviceManager->getDeviceOrFirst(deviceId);
    if (!device) {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Device not found";
        return false;
    }

    // Get actual device ID from the device instance (no redundant lookup needed)
    const QString actualDeviceId = device->deviceId();

    // Check if credential requires touch BEFORE calling generateCode() to avoid blocking
    // Also find the credential object for formatting
    QList<OathCredential> credentials = m_deviceManager->getCredentials();
    bool requiresTouch = false;
    OathCredential foundCredential;
    bool credentialFound = false;

    for (const auto &cred : credentials) {
        if (cred.name == credentialName && cred.deviceId == actualDeviceId) {
            requiresTouch = cred.requiresTouch;
            foundCredential = cred;
            credentialFound = true;
            qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Credential" << credentialName
                                        << "requiresTouch:" << requiresTouch;
            break;
        }
    }

    if (!credentialFound) {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Credential not found:" << credentialName;
        return false;
    }

    // Format credential display name according to configuration (same as KRunner)
    QString deviceName;
    auto dbRecord = m_database->getDevice(actualDeviceId);
    if (dbRecord.has_value()) {
        deviceName = dbRecord->deviceName;
    }

    int connectedDeviceCount = m_deviceManager->getConnectedDeviceIds().size();

    QString formattedTitle = CredentialFormatter::formatDisplayName(
        foundCredential,
        m_config->showUsername(),
        false, // Don't show code in title (code is shown in notification body)
        m_config->showDeviceName(),
        deviceName,
        connectedDeviceCount,
        m_config->showDeviceNameOnlyWhenMultiple()
    );

    // If touch required, start async touch workflow to avoid blocking
    if (requiresTouch) {
        qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Touch required, starting async touch workflow";
        m_touchWorkflowCoordinator->startTouchWorkflow(credentialName, actionType, actualDeviceId);
        return true; // Workflow started successfully
    }

    // No touch required - generate code synchronously and execute with notification
    auto codeResult = device->generateCode(credentialName);
    if (codeResult.isError()) {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Failed to generate code:" << codeResult.error();
        return false;
    }

    const QString code = codeResult.value();

    // Use unified action execution with notification handling (pass formatted title)
    ActionExecutor::ActionResult result = executeActionWithNotification(code, formattedTitle, actionType);
    return result == ActionExecutor::ActionResult::Success;
}

} // namespace Daemon
} // namespace YubiKeyOath
