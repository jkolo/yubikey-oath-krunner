/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_action_coordinator.h"
#include "action_executor.h"
#include "../services/yubikey_service.h"
#include "../clipboard/clipboard_manager.h"
#include "../input/text_input_factory.h"
#include "../notification/dbus_notification_manager.h"
#include "../workflows/notification_orchestrator.h"
#include "../workflows/notification_helper.h"
#include "../workflows/touch_handler.h"
#include "../workflows/touch_workflow_coordinator.h"
#include "../workflows/reconnect_workflow_coordinator.h"
#include "../cache/credential_cache_searcher.h"
#include "../oath/yubikey_device_manager.h"
#include "../storage/yubikey_database.h"
#include "../config/daemon_configuration.h"
#include "../logging_categories.h"
#include "formatting/credential_formatter.h"
#include "utils/credential_finder.h"
#include "utils/device_name_formatter.h"

#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

namespace {
// Helper function to convert string action type to OperationType enum
TouchWorkflowCoordinator::OperationType stringToOperationType(const QString &actionType)
{
    if (actionType == QStringLiteral("generate")) {
        return TouchWorkflowCoordinator::OperationType::Generate;
    } else if (actionType == QStringLiteral("copy")) {
        return TouchWorkflowCoordinator::OperationType::Copy;
    } else if (actionType == QStringLiteral("type")) {
        return TouchWorkflowCoordinator::OperationType::Type;
    } else if (actionType == QStringLiteral("delete")) {
        return TouchWorkflowCoordinator::OperationType::Delete;
    } else {
        // Default to Generate for unknown action types
        qCWarning(YubiKeyActionCoordinatorLog) << "Unknown action type:" << actionType << "- defaulting to Generate";
        return TouchWorkflowCoordinator::OperationType::Generate;
    }
}
} // anonymous namespace

YubiKeyActionCoordinator::YubiKeyActionCoordinator(YubiKeyService *service,
                                         YubiKeyDeviceManager *deviceManager,
                                         YubiKeyDatabase *database,
                                         SecretStorage *secretStorage,
                                         DaemonConfiguration *config,
                                         QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_secretStorage(secretStorage)
    , m_config(config)
    , m_clipboardManager(std::make_unique<ClipboardManager>(this))
    , m_textInput(TextInputFactory::createProvider(secretStorage, this))
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
    , m_reconnectWorkflowCoordinator(std::make_unique<ReconnectWorkflowCoordinator>(
        service,  // Pass service for signals and device access
        database,
        this, // Pass action coordinator for unified action execution
        m_notificationOrchestrator.get(),
        config,
        this))
    , m_cacheSearcher(std::make_unique<CredentialCacheSearcher>(
        deviceManager,
        database,
        config))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyActionCoordinator: Initialized with touch and reconnect workflow support";

    // Pre-initialize text input provider (e.g., create Portal session in advance)
    // This reduces latency on first use by avoiding lazy initialization delay
    if (m_textInput) {
        qCDebug(YubiKeyActionCoordinatorLog) << "Pre-initializing text input provider:" << m_textInput->providerName();
        m_textInput->preInitialize();
    }
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
                                                                                    const QString &actionType,
                                                                                    const Shared::DeviceModel& deviceModel)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: executeActionWithNotification"
                                << "action:" << actionType << "credential:" << credentialName
                                << "brand:" << brandName(deviceModel.brand)
                                << "model:" << deviceModel.modelString;

    // Execute action based on type
    ActionExecutor::ActionResult result = ActionExecutor::ActionResult::Failed;
    if (actionType == QStringLiteral("copy")) {
        result = m_actionExecutor->executeCopyAction(code, credentialName);

        // Copy action: always show notification on success
        if (result == ActionExecutor::ActionResult::Success) {
            const int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);
            m_notificationOrchestrator->showCodeNotification(code, credentialName, totalSeconds, deviceModel);
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

void YubiKeyActionCoordinator::showSimpleNotification(const QString &title, const QString &message, int type)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: showSimpleNotification"
                                << "title:" << title;
    m_notificationOrchestrator->showSimpleNotification(title, message, type);
}

uint YubiKeyActionCoordinator::showPersistentNotification(const QString &title, const QString &message, int type)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: showPersistentNotification"
                                << "title:" << title;
    return m_notificationOrchestrator->showPersistentNotification(title, message, type);
}

void YubiKeyActionCoordinator::closeNotification(uint notificationId)
{
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: closeNotification"
                                << "id:" << notificationId;
    m_notificationOrchestrator->closeNotification(notificationId);
}

bool YubiKeyActionCoordinator::tryStartReconnectWorkflow(
    const QString &deviceId,
    const QString &credentialName,
    const QString &actionType)
{
    qCDebug(YubiKeyActionCoordinatorLog)
        << "YubiKeyActionCoordinator: Starting reconnect workflow for cached credential";

    m_reconnectWorkflowCoordinator->startReconnectWorkflow(
        deviceId,
        credentialName,
        actionType
    );

    return true; // Workflow started successfully
}

bool YubiKeyActionCoordinator::executeActionInternal(const QString &deviceId,
                                                     const QString &credentialName,
                                                     const QString &actionType)
{
    // Get device (use first connected if deviceId empty)
    auto *device = m_deviceManager->getDeviceOrFirst(deviceId);

    // If device not found, try cached credential (offline device with cached credentials)
    if (!device) {
        qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Device not connected, checking cache";

        auto cachedDeviceId = m_cacheSearcher->findCachedCredentialDevice(credentialName, deviceId);
        if (cachedDeviceId.has_value()) {
            return tryStartReconnectWorkflow(*cachedDeviceId, credentialName, actionType);
        }

        // Not found in cache
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Device not found"
                                                << (m_config->enableCredentialsCache() ? "and not in cache" : "(cache disabled)");
        return false;
    }

    // Get actual device ID from the device instance (no redundant lookup needed)
    const QString actualDeviceId = device->deviceId();

    // Check if credential requires touch BEFORE calling generateCode() to avoid blocking
    // Also find the credential object for formatting
    const QList<OathCredential> credentials = m_deviceManager->getCredentials();

    // Find credential using shared utility function
    auto foundCredentialOpt = Utils::findCredential(credentials, credentialName, actualDeviceId);
    if (!foundCredentialOpt.has_value()) {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Credential not found:" << credentialName;
        return false;
    }

    const OathCredential &foundCredential = foundCredentialOpt.value();
    const bool requiresTouch = foundCredential.requiresTouch;
    qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Credential" << credentialName
                                << "requiresTouch:" << requiresTouch;

    // Format credential display name according to configuration (same as KRunner)
    const QString deviceName = DeviceNameFormatter::getDeviceDisplayName(actualDeviceId, m_database);

    const int connectedDeviceCount = static_cast<int>(m_deviceManager->getConnectedDeviceIds().size());

    const FormatOptions options = FormatOptionsBuilder()
        .withUsername(m_config->showUsername())
        .withCode(false) // Don't show code in title (code is shown in notification body)
        .withDevice(deviceName, m_config->showDeviceName())
        .withDeviceCount(connectedDeviceCount)
        .onlyWhenMultipleDevices(m_config->showDeviceNameOnlyWhenMultiple())
        .build();

    const QString formattedTitle = CredentialFormatter::formatDisplayName(foundCredential, options);

    // If touch required, start async touch workflow to avoid blocking
    if (requiresTouch) {
        qCDebug(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Touch required, starting async touch workflow";
        const Shared::DeviceModel deviceModel = device->deviceModel();
        const TouchWorkflowCoordinator::OperationType opType = stringToOperationType(actionType);
        m_touchWorkflowCoordinator->startTouchWorkflow(credentialName, opType, actualDeviceId, deviceModel);
        return true; // Workflow started successfully
    }

    // No touch required - generate code synchronously and execute with notification
    auto codeResult = device->generateCode(credentialName);
    if (codeResult.isError()) {
        qCWarning(YubiKeyActionCoordinatorLog) << "YubiKeyActionCoordinator: Failed to generate code:" << codeResult.error();
        return false;
    }

    const QString code = codeResult.value();

    // Get device model for notification icon
    const Shared::DeviceModel deviceModel = device->deviceModel();

    // Use unified action execution with notification handling (pass formatted title and device model)
    const ActionExecutor::ActionResult result = executeActionWithNotification(code, formattedTitle, actionType, deviceModel);
    return result == ActionExecutor::ActionResult::Success;
}

} // namespace Daemon
} // namespace YubiKeyOath
