/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include "action_executor.h"  // Required for ActionExecutor::ActionResult

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class ClipboardManager;
class TextInputProvider;
class DBusNotificationManager;
class NotificationOrchestrator;
class DaemonConfiguration;
class YubiKeyDeviceManager;
class YubiKeyDatabase;
class SecretStorage;
class TouchHandler;
class TouchWorkflowCoordinator;

/**
 * @brief Coordinates YubiKey actions: copy, type, add credential
 *
 * Single Responsibility: Coordinate YubiKey actions by checking touch requirements
 * and delegating to appropriate components (ActionExecutor, TouchWorkflowCoordinator).
 *
 * This class aggregates all action-related components and provides
 * high-level methods for D-Bus service to call. It handles the decision logic
 * of whether to start a touch workflow or execute the action directly.
 */
class YubiKeyActionCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKey action coordinator
     * @param deviceManager YubiKey device manager for operations
     * @param database YubiKey database for device information
     * @param secretStorage Secret storage for KWallet operations
     * @param config Daemon configuration
     * @param parent Parent QObject
     */
    explicit YubiKeyActionCoordinator(YubiKeyDeviceManager *deviceManager,
                                     YubiKeyDatabase *database,
                                     SecretStorage *secretStorage,
                                     DaemonConfiguration *config,
                                     QObject *parent = nullptr);

    ~YubiKeyActionCoordinator() override;

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
    // Use addCredential() in YubiKeyService for direct credential adding

    /**
     * @brief Executes action with code and shows notification according to policy
     * @param code TOTP/HOTP code to use
     * @param credentialName Credential name for notifications
     * @param actionType "copy" or "type"
     * @return ActionResult (Success, Failed, WaitingForPermission)
     *
     * Notification policy:
     * - Copy action: always shows notification on success
     * - Type action: never shows notification (user sees code being typed)
     * - Both: show error notification on failure
     *
     * This method unifies action execution logic used by both direct execution
     * and touch workflow coordinator.
     */
    ActionExecutor::ActionResult executeActionWithNotification(const QString &code,
                                                               const QString &credentialName,
                                                               const QString &actionType);

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

private:
    /**
     * @brief Helper method to execute an action (copy or type)
     * @param deviceId Device ID
     * @param credentialName Credential name
     * @param actionType "copy" or "type"
     * @return true on success, false on failure
     *
     * This method handles the common logic for both copy and type actions:
     * - Device lookup
     * - Touch requirement checking
     * - Touch workflow coordination OR direct code generation
     * - Action execution via ActionExecutor
     */
    bool executeActionInternal(const QString &deviceId,
                              const QString &credentialName,
                              const QString &actionType);

    YubiKeyDeviceManager *m_deviceManager;
    YubiKeyDatabase *m_database;
    SecretStorage *m_secretStorage;
    DaemonConfiguration *m_config;

    std::unique_ptr<ClipboardManager> m_clipboardManager;
    std::unique_ptr<TextInputProvider> m_textInput;
    std::unique_ptr<DBusNotificationManager> m_notificationManager;
    std::unique_ptr<NotificationOrchestrator> m_notificationOrchestrator;
    std::unique_ptr<ActionExecutor> m_actionExecutor;
    std::unique_ptr<TouchHandler> m_touchHandler;
    std::unique_ptr<TouchWorkflowCoordinator> m_touchWorkflowCoordinator;
};

} // namespace Daemon
} // namespace YubiKeyOath
