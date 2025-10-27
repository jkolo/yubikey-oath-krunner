/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "logging_categories.h"

namespace YubiKeyOath {
namespace Runner {

// Main runner component
Q_LOGGING_CATEGORY(YubiKeyRunnerLog, "pl.jkolo.yubikey.oath.daemon.runner", QtWarningMsg)

// Orchestration layer
Q_LOGGING_CATEGORY(NotificationOrchestratorLog, "pl.jkolo.yubikey.oath.daemon.notification", QtWarningMsg)
Q_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog, "pl.jkolo.yubikey.oath.daemon.touch", QtWarningMsg)
Q_LOGGING_CATEGORY(ActionExecutorLog, "pl.jkolo.yubikey.oath.daemon.action", QtWarningMsg)
Q_LOGGING_CATEGORY(MatchBuilderLog, "pl.jkolo.yubikey.oath.daemon.match", QtWarningMsg)

// Infrastructure layer
Q_LOGGING_CATEGORY(TextInputLog, "pl.jkolo.yubikey.oath.daemon.input", QtWarningMsg)
Q_LOGGING_CATEGORY(DBusNotificationLog, "pl.jkolo.yubikey.oath.daemon.dbus", QtWarningMsg)

} // namespace Runner
} // namespace YubiKeyOath
