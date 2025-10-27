/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

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
 * 1. Show touch notification via NotificationOrchestrator
 * 2. Start async code generation via ICredentialProvider
 * 3. Wait for codeGenerated signal (user touched YubiKey)
 * 4. Close touch notification
 * 5. Execute action (copy/type) via ActionExecutor
 * 6. Show code notification (for copy action only)
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
 * coordinator->startTouchWorkflow("Google:user@example.com", "copy", "ABC123");
 * // Shows touch notification, waits for touch, then copies code
 *
 * // Start workflow for type action
 * coordinator->startTouchWorkflow("Google:user@example.com", "type", "ABC123");
 * // Shows touch notification, waits for touch, then types code
 * @endcode
 */
class TouchWorkflowCoordinator : public QObject
{
    Q_OBJECT

public:
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
     * 1. Displays touch notification with countdown
     * 2. Starts async code generation (blocks waiting for touch on YubiKey)
     * 3. On success: executes action and shows result
     * 4. On timeout/cancel: cleans up and notifies user
     *
     * @param credentialName Full credential name (e.g., "Google:user@example.com")
     * @param actionId Action to execute after touch: "copy" or "type"
     * @param deviceId Device ID for multi-device support. If empty, uses default device.
     *
     * @note Only one workflow can be active at a time. Calling this while
     *       another workflow is in progress cancels the previous one.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void startTouchWorkflow(const QString &credentialName, const QString &actionId, const QString &deviceId);

private Q_SLOTS:
    void onCodeGenerated(const QString &credentialName, const QString &code);
    void onCodeGenerationFailed(const QString &credentialName, const QString &error);
    void onTouchTimeout(const QString &credentialName);
    void onTouchCancelled();

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

    QString m_pendingActionId; // Action to execute after touch
    QString m_pendingDeviceId; // Device ID for pending touch operation
};

} // namespace Daemon
} // namespace YubiKeyOath
