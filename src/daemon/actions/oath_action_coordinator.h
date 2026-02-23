/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>
#include "action_executor.h"  // Required for ActionExecutor::ActionResult
#include "../../shared/types/yubikey_model.h"  // Required for YubiKeyModel type

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathService;
class ClipboardManager;
class TextInputProvider;
class DBusNotificationManager;
class NotificationOrchestrator;
class DaemonConfiguration;
class OathDeviceManager;
class OathDatabase;
class SecretStorage;
class TouchHandler;
class TouchWorkflowCoordinator;
class ReconnectWorkflowCoordinator;
class CredentialCacheSearcher;

/**
 * @brief Coordinates YubiKey actions: copy, type, add credential
 *
 * Single Responsibility: Coordinate YubiKey actions by checking touch requirements
 * and delegating to appropriate components (ActionExecutor, TouchWorkflowCoordinator,
 * ReconnectWorkflowCoordinator, CredentialCacheSearcher).
 *
 * This class aggregates all action-related components and provides
 * high-level methods for D-Bus service to call. It handles the decision logic
 * of whether to start a touch workflow, reconnect workflow, or execute the action directly.
 */
class OathActionCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKey action coordinator
     * @param service YubiKey service (for reconnect workflow)
     * @param deviceManager YubiKey device manager for operations
     * @param database YubiKey database for device information
     * @param secretStorage Secret storage for KWallet operations
     * @param config Daemon configuration
     * @param parent Parent QObject
     */
    explicit OathActionCoordinator(OathService *service,
                                     OathDeviceManager *deviceManager,
                                     OathDatabase *database,
                                     SecretStorage *secretStorage,
                                     DaemonConfiguration *config,
                                     QObject *parent = nullptr);

    ~OathActionCoordinator() override;

    /**
     * @brief Copies TOTP code to clipboard
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @return true on success, false on failure
     */
    bool copyCodeToClipboard(const QString &deviceId, const QString &credentialName);

    /**
     * @brief Types TOTP code via keyboard emulation
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @return true on success, false on failure
     */
    bool typeCode(const QString &deviceId, const QString &credentialName);

    // Note: addCredentialFromScreen() removed - not yet implemented
    // Use addCredential() in OathService for direct credential adding

    /**
     * @brief Executes action with code and shows notification according to policy
     * @param code TOTP/HOTP code to use
     * @param credentialName Credential name for notifications
     * @param actionType "copy" or "type"
     * @param deviceModel Device model for brand-specific notification icon
     * @return ActionResult (Success, Failed, WaitingForPermission)
     *
     * Notification policy:
     * - Copy action: always shows notification on success with device-specific icon
     * - Type action: never shows notification (user sees code being typed)
     * - Both: show error notification on failure
     *
     * This method unifies action execution logic used by both direct execution
     * and touch workflow coordinator.
     */
    ActionExecutor::ActionResult executeActionWithNotification(const QString &code,
                                                               const QString &credentialName,
                                                               const QString &actionType,
                                                               const Shared::DeviceModel& deviceModel);

    /**
     * @brief Shows simple auto-closing notification
     * @param title Notification title
     * @param message Notification message
     * @param type Notification type (0 = info, 1 = warning/error)
     *
     * Delegates to NotificationOrchestrator for showing simple notification.
     */
    void showSimpleNotification(const QString &title, const QString &message, int type = 0);

    /**
     * @brief Shows persistent notification that stays until closed
     * @param title Notification title
     * @param message Notification message
     * @param type Notification type (0 = info, 1 = warning/error)
     * @return Notification ID (use with closeNotification to close)
     *
     * Delegates to NotificationOrchestrator for showing persistent notification.
     */
    uint showPersistentNotification(const QString &title, const QString &message, int type = 0);

    /**
     * @brief Closes notification by ID
     * @param notificationId ID returned by showPersistentNotification()
     *
     * Delegates to NotificationOrchestrator for closing notification.
     */
    void closeNotification(uint notificationId);

    /**
     * @brief Shows touch notification for D-Bus async API
     * @param credentialName Credential name
     * @param timeoutSeconds Timeout in seconds
     * @param deviceModel Device model for icon
     *
     * Used by OathCredentialObject when generating code for touch-required credentials.
     * Delegates to NotificationOrchestrator for showing touch notification.
     */
    void showTouchNotification(const QString &credentialName,
                               int timeoutSeconds,
                               const Shared::DeviceModel& deviceModel);

    /**
     * @brief Closes touch notification
     *
     * Called after code is generated successfully or on timeout.
     * Delegates to NotificationOrchestrator.
     */
    void closeTouchNotification();

    /**
     * @brief Gets configured touch timeout
     * @return Touch timeout in seconds
     */
    int touchTimeout() const;

    /**
     * @brief Executes an action (copy or type) with full workflow support
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param actionType "copy" or "type"
     * @return true on success, false on failure
     *
     * This method handles the complete action workflow:
     * - Device lookup and validation
     * - Credential lookup
     * - Touch requirement checking
     * - Touch workflow coordination (async) OR direct code generation
     * - Action execution via ActionExecutor with notifications
     *
     * Used by both direct action execution and reconnect workflow.
     */
    bool executeActionInternal(const QString &deviceId,
                              const QString &credentialName,
                              const QString &actionType);

    /**
     * @brief Copies pre-generated code to clipboard without code generation
     * @param code TOTP/HOTP code to copy
     * @param credentialName Credential name for notifications
     * @return ActionResult (Success, Failed, WaitingForPermission)
     *
     * Use this method when you already have a generated code and just need
     * to copy it to clipboard. Does NOT generate code - use generateCodeAsync()
     * for that. Useful for async workflows where code generation and
     * clipboard operations are separate steps.
     */
    ActionExecutor::ActionResult executeCopyOnly(const QString &code,
                                                  const QString &credentialName);

    /**
     * @brief Types pre-generated code without code generation
     * @param code TOTP/HOTP code to type
     * @param credentialName Credential name for notifications
     * @return ActionResult (Success, Failed, WaitingForPermission)
     *
     * Use this method when you already have a generated code and just need
     * to type it. Does NOT generate code - use generateCodeAsync() for that.
     * Useful for async workflows where code generation and typing are separate steps.
     * Handles modifier key checking and Portal permission dialogs.
     */
    ActionExecutor::ActionResult executeTypeOnly(const QString &code,
                                                  const QString &credentialName);

private:

    /**
     * @brief Starts reconnect workflow for offline device
     * @param deviceId Device ID to reconnect
     * @param credentialName Credential name
     * @param actionType Action to execute after reconnect
     * @return true (workflow started)
     */
    bool tryStartReconnectWorkflow(const QString &deviceId,
                                    const QString &credentialName,
                                    const QString &actionType);

    OathService *m_service;  // Not owned - for accessing services with cached credentials logic
    OathDeviceManager *m_deviceManager;  // Not owned - passed to TouchWorkflow and CacheSearcher
    OathDatabase *m_database;
    SecretStorage *m_secretStorage;
    DaemonConfiguration *m_config;

    std::unique_ptr<ClipboardManager> m_clipboardManager;
    std::unique_ptr<TextInputProvider> m_textInput;
    std::unique_ptr<DBusNotificationManager> m_notificationManager;
    std::unique_ptr<NotificationOrchestrator> m_notificationOrchestrator;
    std::unique_ptr<ActionExecutor> m_actionExecutor;
    std::unique_ptr<TouchHandler> m_touchHandler;
    std::unique_ptr<TouchWorkflowCoordinator> m_touchWorkflowCoordinator;
    std::unique_ptr<ReconnectWorkflowCoordinator> m_reconnectWorkflowCoordinator;
    std::unique_ptr<CredentialCacheSearcher> m_cacheSearcher;
};

} // namespace Daemon
} // namespace YubiKeyOath
