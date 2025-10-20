/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

namespace KRunner {
namespace YubiKey {

// Forward declarations
class TextInputProvider;
class ClipboardManager;
class ConfigurationProvider;
class NotificationOrchestrator;

/**
 * @brief Executes user actions (type/copy) with error handling
 *
 * Single Responsibility: Execute type and copy actions with appropriate fallback logic
 * Open/Closed: Easy to extend with new action types
 *
 * @par Fallback Strategy
 * - Type action: Attempts typing via input provider, falls back to clipboard on failure
 * - Copy action: Direct clipboard copy, no fallback
 *
 * @par Input Methods
 * Supports multiple input methods via TextInputProvider:
 * - Portal (org.freedesktop.portal.RemoteDesktop) - works across X11/Wayland
 * - Wayland (libei) - native Wayland input emulation
 * - X11 (XTest) - X11 keyboard simulation
 *
 * @par Thread Safety
 * All public methods must be called from main/UI thread (QObject-based).
 *
 * @par Usage Example
 * @code
 * ActionExecutor *executor = new ActionExecutor(textInput, clipboard, config);
 *
 * // Connect to notification signal
 * connect(executor, &ActionExecutor::notificationRequested,
 *         notifOrchestrator, &NotificationOrchestrator::showSimpleNotification);
 *
 * // Execute type action (with clipboard fallback)
 * ActionExecutor::ActionResult result = executor->executeTypeAction("123456", "Google");
 * if (result == ActionExecutor::ActionResult::WaitingForPermission) {
 *     qDebug() << "Waiting for user to approve Portal permission...";
 * } else if (result == ActionExecutor::ActionResult::Success) {
 *     qDebug() << "Code typed successfully";
 * }
 *
 * // Execute copy action (no fallback)
 * result = executor->executeCopyAction("123456", "Google");
 * if (result == ActionExecutor::ActionResult::Success) {
 *     qDebug() << "Code copied to clipboard";
 * }
 * @endcode
 */
class ActionExecutor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Result of action execution
     */
    enum class ActionResult {
        Success,              ///< Action completed successfully
        Failed,               ///< Action failed completely (rare - usually has fallback)
        WaitingForPermission  ///< Waiting for user to approve permission dialog (Portal only)
    };

    /**
     * @brief Constructs action executor
     *
     * @param textInput Text input provider for typing (Portal/Wayland/X11)
     * @param clipboardManager Clipboard manager for copy operations
     * @param config Configuration provider for notification preferences
     * @param notificationOrchestrator Notification orchestrator for modifier key warnings
     * @param parent Parent QObject for automatic cleanup
     */
    explicit ActionExecutor(TextInputProvider *textInput,
                           ClipboardManager *clipboardManager,
                           const ConfigurationProvider *config,
                           NotificationOrchestrator *notificationOrchestrator,
                           QObject *parent = nullptr);

    /**
     * @brief Executes type action with automatic fallback to clipboard
     *
     * Attempts to type code using text input provider. If typing fails
     * (e.g., Portal permission denied, input method unavailable), automatically
     * falls back to clipboard copy.
     *
     * @param code TOTP code to type (typically 6-8 digits)
     * @param credentialName Credential name for notification messages
     *
     * @return ActionResult::Success - Code typed or copied (fallback) successfully
     *         ActionResult::WaitingForPermission - Portal permission dialog shown
     *         ActionResult::Failed - Both typing and clipboard failed (very rare)
     *
     * @note Emits notificationRequested() signal on fallback or failure.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    ActionResult executeTypeAction(const QString &code, const QString &credentialName);

    /**
     * @brief Executes copy action (clipboard only, no fallback)
     *
     * Copies TOTP code to system clipboard. No fallback mechanism - if
     * clipboard access fails, operation fails.
     *
     * @param code TOTP code to copy
     * @param credentialName Credential name (currently unused, for future notifications)
     *
     * @return ActionResult::Success - Code copied to clipboard
     *         ActionResult::Failed - Clipboard operation failed (rare)
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    ActionResult executeCopyAction(const QString &code, const QString &credentialName);

Q_SIGNALS:
    /**
     * @brief Emitted when action requires showing a notification
     * @param title Notification title
     * @param message Notification message
     * @param type Notification type (0=info, 1=warning)
     */
    void notificationRequested(const QString &title, const QString &message, int type);

private:
    /**
     * @brief Checks for pressed modifier keys and waits for release
     *
     * Workflow:
     * 1. Check if modifiers are pressed
     * 2. Wait 500ms for release (silent polling)
     * 3. If still pressed, show notification and wait up to 15s
     * 4. If timeout, show cancel notification and fail
     *
     * @param credentialName Credential name for logging
     * @return ActionResult::Success if modifiers released or not pressed
     *         ActionResult::Failed if timeout waiting for release
     */
    ActionResult checkAndWaitForModifiers(const QString &credentialName);

    TextInputProvider *m_textInput;
    ClipboardManager *m_clipboardManager;
    const ConfigurationProvider *m_config;
    NotificationOrchestrator *m_notificationOrchestrator;
};

} // namespace YubiKey
} // namespace KRunner
