/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "action_manager.h"
#include "../logging_categories.h"

#include <KRunner/Action>
#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Runner {

QString ActionManager::determineAction(const KRunner::QueryMatch &match,
                                      const QString &primaryAction) const
{
    // Get selected action from KRunner (set when user clicks action button)
    KRunner::Action selectedAction = match.selectedAction();
    QString selectedActionId = selectedAction.id();

    qCDebug(ActionExecutorLog) << "=========== ActionManager::determineAction() START ===========";
    qCDebug(ActionExecutorLog) << "  match.id():" << match.id();
    qCDebug(ActionExecutorLog) << "  selectedAction.id():" << selectedActionId;
    qCDebug(ActionExecutorLog) << "  primary action from config:" << primaryAction;

    // Log all available actions on the match
    const auto actions = match.actions();
    qCDebug(ActionExecutorLog) << "  match has" << actions.size() << "action(s):";
    for (int i = 0; i < actions.size(); ++i) {
        qCDebug(ActionExecutorLog) << "    [" << i << "]:" << actions[i].id() << "-" << actions[i].text();
    }

    // New logic: Only one action button exists (the alternative action)
    // - If selectedActionId is empty → user pressed Enter → use primary action from config
    // - If selectedActionId is set → user clicked action button → use that action
    if (!selectedActionId.isEmpty()) {
        qCDebug(ActionExecutorLog) << "User clicked action button:" << selectedActionId;

        // Validate that the selected action is recognized
        if (isValidAction(selectedActionId)) {
            qCDebug(ActionExecutorLog) << "Using selected action:" << selectedActionId;
            qCDebug(ActionExecutorLog) << "=========== ActionManager::determineAction() END (button clicked) ===========";
            return selectedActionId;
        } else {
            qCWarning(ActionExecutorLog) << "Invalid selected action ID:" << selectedActionId
                                        << "- falling back to primary action:" << primaryAction;
        }
    } else {
        qCDebug(ActionExecutorLog) << "No action selected (Enter pressed) - using primary action from config";
    }

    // Use primary action from configuration (triggered by Enter without action)
    if (isValidAction(primaryAction)) {
        qCDebug(ActionExecutorLog) << "Using primary action from config:" << primaryAction;
        qCDebug(ActionExecutorLog) << "=========== ActionManager::determineAction() END (primary from config) ===========";
        return primaryAction;
    }

    // Ultimate fallback - should never happen
    qCWarning(ActionExecutorLog) << "Invalid primary action:" << primaryAction
                                << "- falling back to 'copy'";
    qCDebug(ActionExecutorLog) << "=========== ActionManager::determineAction() END (ultimate fallback) ===========";
    return QStringLiteral("copy");
}

bool ActionManager::isValidAction(const QString &actionId) const
{
    return actionId == QStringLiteral("copy") ||
           actionId == QStringLiteral("type") ||
           actionId == QStringLiteral("delete");
}

QString ActionManager::getActionName(const QString &actionId) const
{
    if (actionId == QStringLiteral("copy")) {
        return i18n("Copy to clipboard");
    } else if (actionId == QStringLiteral("type")) {
        return i18n("Type code");
    } else if (actionId == QStringLiteral("delete")) {
        return i18n("Delete credential");
    }
    return i18n("Unknown action");
}

} // namespace Runner
} // namespace YubiKeyOath
