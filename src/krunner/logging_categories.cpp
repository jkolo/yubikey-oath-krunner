/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "logging_categories.h"

namespace KRunner {
namespace YubiKey {

// Main runner component
Q_LOGGING_CATEGORY(YubiKeyRunnerLog, "org.kde.plasma.krunner.yubikey.runner", QtWarningMsg)

// Orchestration layer
Q_LOGGING_CATEGORY(NotificationOrchestratorLog, "org.kde.plasma.krunner.yubikey.notification", QtWarningMsg)
Q_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog, "org.kde.plasma.krunner.yubikey.touch", QtWarningMsg)
Q_LOGGING_CATEGORY(ActionExecutorLog, "org.kde.plasma.krunner.yubikey.action", QtWarningMsg)
Q_LOGGING_CATEGORY(MatchBuilderLog, "org.kde.plasma.krunner.yubikey.match", QtWarningMsg)

// Infrastructure layer
Q_LOGGING_CATEGORY(TextInputLog, "org.kde.plasma.krunner.yubikey.input", QtWarningMsg)
Q_LOGGING_CATEGORY(DBusNotificationLog, "org.kde.plasma.krunner.yubikey.dbus", QtWarningMsg)

} // namespace YubiKey
} // namespace KRunner
