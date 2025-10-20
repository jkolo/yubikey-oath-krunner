/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "action_executor.h"
#include "../input/text_input_provider.h"
#include "../clipboard/clipboard_manager.h"
#include "../workflows/notification_helper.h"
#include "../logging_categories.h"
#include "../config/configuration_provider.h"

#include <KLocalizedString>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

ActionExecutor::ActionExecutor(TextInputProvider *textInput,
                               ClipboardManager *clipboardManager,
                               const ConfigurationProvider *config,
                               QObject *parent)
    : QObject(parent)
    , m_textInput(textInput)
    , m_clipboardManager(clipboardManager)
    , m_config(config)
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

} // namespace YubiKey
} // namespace KRunner
