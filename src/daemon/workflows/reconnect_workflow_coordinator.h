/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>
#include "common/result.h"
#include "types/oath_credential.h"

namespace YubiKeyOath {
namespace Shared {
class ConfigurationProvider;
class ICredentialUpdateNotifier;
struct OathCredential;  // Forward declaration
template<typename T> class Result;  // Forward declaration
}

namespace Daemon {
using Shared::ConfigurationProvider;
using Shared::ICredentialUpdateNotifier;
using Shared::OathCredential;
using Shared::Result;

// Forward declarations
class YubiKeyDatabase;
class YubiKeyActionCoordinator;
class NotificationOrchestrator;
class YubiKeyOathDevice;

/**
 * @brief Coordinates the workflow for reconnecting to offline YubiKeys
 *
 * Single Responsibility: Orchestrate the complete reconnect workflow
 * - Detect when user tries to access cached credential for offline device
 * - Show reconnect notification with timeout
 * - Wait for device reconnection with configurable timeout
 * - Execute action after successful reconnection
 * - Handle reconnect timeout and cancellation
 *
 * @par Workflow Sequence
 * 1. Show reconnect notification via NotificationOrchestrator
 * 2. Start timeout timer based on configuration
 * 3. Wait for deviceConnected signal from DeviceManager
 * 4. On reconnect: generate code and execute action
 * 5. Show result notification
 *
 * @par Timeout Handling
 * - QTimer monitors timeout (configurable via DeviceReconnectTimeout)
 * - On timeout: close notification, cancel operation, notify user
 * - User can also cancel manually via notification button
 *
 * @par Thread Safety
 * All public methods must be called from main/UI thread (QObject-based).
 *
 * @par Usage Example
 * @code
 * ReconnectWorkflowCoordinator *coordinator = new ReconnectWorkflowCoordinator(
 *     deviceManager, database, actionCoordinator, notifOrchestrator, config);
 *
 * // Start workflow for offline device
 * coordinator->startReconnectWorkflow("device123", "Google:user@example.com", "copy");
 * // Shows reconnect notification, waits for device, then generates and copies code
 * @endcode
 */
class ReconnectWorkflowCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs reconnect workflow coordinator
     *
     * @param notifier Credential update notifier (usually YubiKeyService)
     * @param database Database for device information
     * @param actionCoordinator Action coordinator for unified action execution
     * @param notificationOrchestrator Notification orchestrator for UI feedback
     * @param config Configuration provider for timeout settings
     * @param parent Parent QObject for automatic cleanup
     *
     * @note Automatically connects to credentialsUpdated signal from notifier
     * @note Notifier provides device access through ICredentialUpdateNotifier interface
     */
    explicit ReconnectWorkflowCoordinator(ICredentialUpdateNotifier *notifier,
                                          YubiKeyDatabase *database,
                                          YubiKeyActionCoordinator *actionCoordinator,
                                          NotificationOrchestrator *notificationOrchestrator,
                                          const ConfigurationProvider *config,
                                          QObject *parent = nullptr);

    /**
     * @brief Starts reconnect workflow for cached credential on offline device
     *
     * Initiates complete workflow:
     * 1. Displays reconnect notification with device name and credential
     * 2. Starts timeout timer
     * 3. Waits for device reconnection
     * 4. On success: generates code and executes action
     * 5. On timeout/cancel: cleans up and notifies user
     *
     * @param deviceId Device ID that needs to be reconnected
     * @param credentialName Full credential name (e.g., "Google:user@example.com")
     * @param actionId Action to execute after reconnect: "copy" or "type"
     *
     * @note Only one workflow can be active at a time. Calling this while
     *       another workflow is in progress cancels the previous one.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void startReconnectWorkflow(const QString &deviceId,
                                const QString &credentialName,
                                const QString &actionId);

    /**
     * @brief Checks if a reconnect workflow is currently active
     * @return true if waiting for device reconnection
     */
    bool isWaitingForReconnect() const { return m_waitingForReconnect; }

    /**
     * @brief Gets the device ID we're waiting for
     * @return Device ID or empty string if not waiting
     */
    QString waitingDeviceId() const { return m_pendingDeviceId; }

private Q_SLOTS:
    void onDeviceConnected(const QString &deviceId);
    void onReconnectTimeout();
    void onReconnectCancelled();

private:
    /**
     * @brief Common initialization
     *
     * Connects signals from device manager and notification orchestrator.
     */
    void init();

    /**
     * @brief Cleanup reconnect workflow state
     *
     * Centralized cleanup logic that:
     * - Stops timeout timer
     * - Closes reconnect notification
     * - Clears pending action/device/credential state
     *
     * @note Called from multiple completion paths (success, timeout, cancel)
     */
    void cleanupReconnectWorkflow();

    /**
     * @brief Gets device name for display in notifications
     * @param deviceId Device ID
     * @return Custom name from database or generated default
     */
    QString getDeviceName(const QString &deviceId) const;

    // Helper methods for onDeviceConnected() workflow
    /**
     * @brief Finds credential object from current device credentials
     * @return Optional credential if found
     */
    std::optional<OathCredential> findCredentialForReconnect();

    /**
     * @brief Shows touch notification if credential requires physical touch
     * @param credential Credential to check
     */
    void showTouchNotificationIfRequired(const OathCredential &credential);

    /**
     * @brief Generates code and manages touch notification lifecycle
     * @param device Device to generate code from
     * @param credentialName Credential name
     * @return Result with code or error
     */
    Result<QString> generateCodeWithNotifications(YubiKeyOathDevice *device,
                                                   const QString &credentialName);

    /**
     * @brief Formats credential title for notifications
     * @param credential Credential to format
     * @return Formatted display name
     */
    QString formatCredentialTitle(const OathCredential &credential);

    /**
     * @brief Executes action after successful reconnect
     * @param code Generated TOTP/HOTP code
     * @param title Formatted credential title
     */
    void executeActionAfterReconnect(const QString &code, const QString &title);

    ICredentialUpdateNotifier *m_notifier;
    YubiKeyDatabase *m_database;
    YubiKeyActionCoordinator *m_actionCoordinator;
    NotificationOrchestrator *m_notificationOrchestrator;
    const ConfigurationProvider *m_config;

    // Workflow state
    bool m_waitingForReconnect = false;
    bool m_touchNotificationShown = false;  // Track if touch notification was shown
    QString m_pendingDeviceId;       // Device ID waiting for reconnection
    QString m_pendingCredentialName; // Credential name to generate after reconnect
    QString m_pendingActionId;       // Action to execute after reconnect ("copy" or "type")
    QTimer *m_timeoutTimer;          // Timer for reconnect timeout
};

} // namespace Daemon
} // namespace YubiKeyOath
