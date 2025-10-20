/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "action_executor.h"
#include "../input/text_input_provider.h"
#include "../input/modifier_key_checker.h"
#include "../clipboard/clipboard_manager.h"
#include "../workflows/notification_helper.h"
#include "../workflows/notification_orchestrator.h"
#include "../logging_categories.h"
#include "../config/configuration_provider.h"

#include <KLocalizedString>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

ActionExecutor::ActionExecutor(TextInputProvider *textInput,
                               ClipboardManager *clipboardManager,
                               const ConfigurationProvider *config,
                               NotificationOrchestrator *notificationOrchestrator,
                               QObject *parent)
    : QObject(parent)
    , m_textInput(textInput)
    , m_clipboardManager(clipboardManager)
    , m_config(config)
    , m_notificationOrchestrator(notificationOrchestrator)
{
}

ActionExecutor::ActionResult ActionExecutor::executeTypeAction(const QString &code, const QString &credentialName)
{
    qCDebug(ActionExecutorLog) << "Executing type action for:" << credentialName;

    // Validate input
    if (code.isEmpty()) {
        qCWarning(ActionExecutorLog) << "Cannot type empty code for:" << credentialName;
        Q_EMIT notificationRequested(i18n("Error"),
                                    i18n("No code available to type"),
                                    1);
        return ActionResult::Failed;
    }

    if (!m_textInput) {
        qCWarning(ActionExecutorLog) << "No text input provider available";
        Q_EMIT notificationRequested(i18n("Error"),
                                    i18n("Text input not available"),
                                    1);
        return ActionResult::Failed;
    }

    // Check for pressed modifier keys and wait for release
    ActionResult modifierCheck = checkAndWaitForModifiers(credentialName);
    if (modifierCheck != ActionResult::Success) {
        qCWarning(ActionExecutorLog) << "Type action cancelled due to modifier keys for:" << credentialName;
        return modifierCheck;
    }

    bool success = m_textInput->typeText(code);

    if (success) {
        qCDebug(ActionExecutorLog) << "Code typed successfully for:" << credentialName;
        return ActionResult::Success;
    }

    // Check if user explicitly rejected permission
    if (m_textInput->wasPermissionRejected()) {
        qCWarning(ActionExecutorLog) << "User rejected permission to type code for:" << credentialName;

        // Copy to clipboard as fallback
        executeCopyAction(code, credentialName);

        // Show notification with code visible
        Q_EMIT notificationRequested(i18n("Permission Denied"),
                                    i18n("Code: %1 (copied to clipboard)", code),
                                    1);
        return ActionResult::Failed;
    }

    // Check if we're waiting for permission approval (timeout, not rejection)
    if (m_textInput->isWaitingForPermission()) {
        qCDebug(ActionExecutorLog) << "Waiting for permission approval, will retry automatically";
        // Don't show error notification or fallback to clipboard
        // The portal will connect and typing will work on next attempt
        return ActionResult::WaitingForPermission;
    }

    // Real failure - fallback to clipboard
    qCWarning(ActionExecutorLog) << "Failed to type code for:" << credentialName << "- falling back to clipboard";
    Q_EMIT notificationRequested(i18n("YubiKey OATH"),
                                i18n("Failed to type code, copied to clipboard instead"),
                                1);

    // Execute fallback
    executeCopyAction(code, credentialName);
    return ActionResult::Failed;
}

ActionExecutor::ActionResult ActionExecutor::executeCopyAction(const QString &code, const QString &credentialName)
{
    qCDebug(ActionExecutorLog) << "Executing copy action for:" << credentialName;

    // Validate input
    if (code.isEmpty()) {
        qCWarning(ActionExecutorLog) << "Cannot copy empty code for:" << credentialName;
        Q_EMIT notificationRequested(i18n("Error"),
                                    i18n("No code available to copy"),
                                    1);
        return ActionResult::Failed;
    }

    if (!m_clipboardManager) {
        qCWarning(ActionExecutorLog) << "No clipboard manager available";
        Q_EMIT notificationRequested(i18n("Error"),
                                    i18n("Clipboard not available"),
                                    1);
        return ActionResult::Failed;
    }

    // Calculate code expiration time for clipboard auto-clear
    int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);

    // Copy to clipboard with auto-clear timeout
    if (!m_clipboardManager->copyToClipboard(code, totalSeconds)) {
        qCWarning(ActionExecutorLog) << "Failed to copy to clipboard for:" << credentialName;
        Q_EMIT notificationRequested(i18n("Error"),
                                    i18n("Failed to copy to clipboard"),
                                    1);
        return ActionResult::Failed;
    }

    qCDebug(ActionExecutorLog) << "Code copied to clipboard successfully for:" << credentialName
             << "will clear in:" << totalSeconds << "seconds";
    return ActionResult::Success;
}

ActionExecutor::ActionResult ActionExecutor::checkAndWaitForModifiers(const QString &credentialName)
{
    qCDebug(ActionExecutorLog) << "Checking for pressed modifier keys before typing for:" << credentialName;

    // Check if any modifiers are currently pressed
    if (!ModifierKeyChecker::hasModifiersPressed()) {
        qCDebug(ActionExecutorLog) << "No modifier keys pressed - proceeding with type action";
        return ActionResult::Success;
    }

    // Get list of pressed modifiers for notifications
    QStringList pressedModifiers = ModifierKeyChecker::getPressedModifiers();
    qCDebug(ActionExecutorLog) << "Modifier keys detected:" << pressedModifiers
                               << "- waiting for release";

    // Phase 1: Wait 250ms silently for user to release modifiers
    constexpr int INITIAL_WAIT_MS = 250;
    constexpr int POLL_INTERVAL_MS = 50;

    if (ModifierKeyChecker::waitForModifierRelease(INITIAL_WAIT_MS, POLL_INTERVAL_MS)) {
        qCDebug(ActionExecutorLog) << "Modifiers released within initial" << INITIAL_WAIT_MS
                                   << "ms - proceeding with type action";
        return ActionResult::Success;
    }

    // Phase 2: Still pressed after 500ms - show notification and wait up to 15s
    qCDebug(ActionExecutorLog) << "Modifiers still pressed after" << INITIAL_WAIT_MS
                               << "ms - showing release notification";

    constexpr int RELEASE_WAIT_SECONDS = 15;

    // Show notification requesting release
    if (m_notificationOrchestrator) {
        m_notificationOrchestrator->showModifierReleaseNotification(pressedModifiers, RELEASE_WAIT_SECONDS);
    }

    // Wait for release with notification countdown
    constexpr int RELEASE_WAIT_MS = RELEASE_WAIT_SECONDS * 1000;
    bool released = ModifierKeyChecker::waitForModifierRelease(RELEASE_WAIT_MS, POLL_INTERVAL_MS);

    // Close notification
    if (m_notificationOrchestrator) {
        m_notificationOrchestrator->closeModifierNotification();
    }

    if (released) {
        qCDebug(ActionExecutorLog) << "Modifiers released during notification period - proceeding with type action";
        return ActionResult::Success;
    }

    // Phase 3: Timeout - modifiers still pressed after 15s
    qCWarning(ActionExecutorLog) << "Modifier timeout - keys still pressed after"
                                 << (INITIAL_WAIT_MS + RELEASE_WAIT_MS) << "ms - cancelling type action";

    // Show cancellation notification
    if (m_notificationOrchestrator) {
        m_notificationOrchestrator->showModifierCancelNotification();
    }

    return ActionResult::Failed;
}

} // namespace YubiKey
} // namespace KRunner
