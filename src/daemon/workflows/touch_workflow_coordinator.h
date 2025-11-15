/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include "../../shared/types/yubikey_model.h"

namespace YubiKeyOath {
namespace Shared {
class ConfigurationProvider;
}

namespace Daemon {
using Shared::ConfigurationProvider;

// Forward declarations
class YubiKeyDeviceManager;
class YubiKeyDatabase;
class YubiKeyActionCoordinator;
class TouchHandler;
class NotificationOrchestrator;

/**
 * @brief Coordinates the workflow for touch-required credentials
 *
 * Single Responsibility: Orchestrate the complete touch workflow from start to finish
 * - Start touch operation with notification
 * - Poll for YubiKey touch completion
 * - Execute action after successful touch
 * - Handle touch timeout and cancellation
 *
 * @par Workflow Sequence
 * 1. Emit touchRequired signal (client can show custom notification)
 * 2. Show touch notification via NotificationOrchestrator
 * 3. Start async operation (generate/copy/type/delete)
 * 4. Wait for operation completion (user touched device or timeout)
 * 5. Emit touchCompleted signal
 * 6. Close touch notification
 * 7. Execute action and show result notification (if applicable)
 *
 * @par Timeout Handling
 * - TouchHandler monitors timeout via QTimer
 * - On timeout: close notification, cancel operation, notify user
 * - User can also cancel manually via notification button
 *
 * @par Thread Safety
 * All public methods must be called from main/UI thread (QObject-based).
 *
 * @par Usage Example
 * @code
 * TouchWorkflowCoordinator *coordinator = new TouchWorkflowCoordinator(
 *     credProvider, touchHandler, actionExecutor, notifOrchestrator, config);
 *
 * // Start workflow for copy action
 * coordinator->startTouchWorkflow("Google:user@example.com", OperationType::Copy, "ABC123", deviceModel);
 * // Emits touchRequired, shows touch notification, waits for touch, then copies code
 *
 * // Start workflow for generate action
 * coordinator->startTouchWorkflow("Google:user@example.com", OperationType::Generate, "ABC123", deviceModel);
 * // Emits touchRequired, shows touch notification, waits for touch, then generates code
 * @endcode
 */
class TouchWorkflowCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Type of operation to perform after touch
     */
    enum class OperationType {
        Generate,  ///< Generate TOTP/HOTP code
        Copy,      ///< Copy code to clipboard
        Type,      ///< Type code via input system
        Delete     ///< Delete credential
    };
    /**
     * @brief Constructs touch workflow coordinator
     *
     * @param deviceManager Device manager for direct code generation
     * @param database Database for device information
     * @param actionCoordinator Action coordinator for unified action execution
     * @param touchHandler Touch handler for timeout management
     * @param notificationOrchestrator Notification orchestrator for UI feedback
     * @param config Configuration provider for timeout settings
     * @param parent Parent QObject for automatic cleanup
     *
     * @note Automatically connects to signals from all dependencies.
     * @note Uses actionCoordinator->executeActionWithNotification() for unified action execution policy
     */
    explicit TouchWorkflowCoordinator(YubiKeyDeviceManager *deviceManager,
                                      YubiKeyDatabase *database,
                                      YubiKeyActionCoordinator *actionCoordinator,
                                      TouchHandler *touchHandler,
                                      NotificationOrchestrator *notificationOrchestrator,
                                      const ConfigurationProvider *config,
                                      QObject *parent = nullptr);

    /**
     * @brief Starts touch workflow for a credential requiring touch
     *
     * Initiates complete workflow:
     * 1. Emits touchRequired signal (client can show custom notification)
     * 2. Displays touch notification with countdown
     * 3. Starts async operation (generate/copy/type/delete)
     * 4. On success: executes action, emits touchCompleted(true), shows result
     * 5. On timeout/cancel: emits touchCompleted(false), cleans up and notifies user
     *
     * @param credentialName Full credential name (e.g., "Google:user@example.com")
     * @param operationType Type of operation to perform: Generate, Copy, Type, or Delete
     * @param deviceId Device ID for multi-device support. If empty, uses default device.
     * @param deviceModel Device model for brand-specific notification icon
     *
     * @note Only one workflow can be active at a time. Calling this while
     *       another workflow is in progress cancels the previous one.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void startTouchWorkflow(const QString &credentialName, OperationType operationType, const QString &deviceId, const Shared::DeviceModel& deviceModel);

Q_SIGNALS:
    /**
     * @brief Emitted when user needs to touch the device
     *
     * Client can show custom notification or rely on daemon's notification.
     *
     * @param timeoutSeconds Number of seconds before timeout
     * @param deviceModel Device model string for icon/description (e.g., "YubiKey 5C NFC")
     */
    void touchRequired(int timeoutSeconds, const QString &deviceModel);

    /**
     * @brief Emitted when touch workflow completes
     *
     * @param success true if touch detected and operation continuing, false if cancelled/timeout
     */
    void touchCompleted(bool success);

private Q_SLOTS:
    void onCodeGenerated(const QString &credentialName, const QString &code);
    void onCodeGenerationFailed(const QString &credentialName, const QString &error);
    void onTouchTimeout(const QString &credentialName);
    void onTouchCancelled();
    void onDeviceTouchDetected();

private:
    /**
     * @brief Common initialization for both constructors
     *
     * Connects signals from touch handler and notification orchestrator.
     * Called from both constructors to avoid code duplication.
     */
    void init();
    /**
     * @brief Cleanup touch workflow state
     *
     * Centralized cleanup logic that:
     * - Cancels touch operation timer
     * - Closes touch notification
     * - Clears pending action/device state
     *
     * @note Called from multiple completion paths (success, failure, timeout, cancel)
     */
    void cleanupTouchWorkflow();

    YubiKeyDeviceManager *m_deviceManager;
    YubiKeyDatabase *m_database;
    YubiKeyActionCoordinator *m_actionCoordinator;
    TouchHandler *m_touchHandler;
    NotificationOrchestrator *m_notificationOrchestrator;
    const ConfigurationProvider *m_config;

    OperationType m_pendingOperationType; // Operation type to execute after touch
    QString m_pendingDeviceId; // Device ID for pending touch operation
    Shared::DeviceModel m_pendingDeviceModel; // Device model for notifications
    QString m_pendingCredentialName; // Credential name for delayed notification
    int m_pendingTouchTimeout{15}; // Touch timeout in seconds for delayed notification
    QMetaObject::Connection m_deviceConnection; // Connection to device touchRequired signal
};

} // namespace Daemon
} // namespace YubiKeyOath
