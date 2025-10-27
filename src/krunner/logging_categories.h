/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QLoggingCategory>

namespace YubiKeyOath {
namespace Runner {

/**
 * @brief Qt Logging Categories for KRunner YubiKey Plugin
 *
 * Control via environment:
 *   QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true"
 */

// Main runner component
Q_DECLARE_LOGGING_CATEGORY(YubiKeyRunnerLog)

// Orchestration layer
Q_DECLARE_LOGGING_CATEGORY(NotificationOrchestratorLog)
Q_DECLARE_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog)
Q_DECLARE_LOGGING_CATEGORY(ActionExecutorLog)
Q_DECLARE_LOGGING_CATEGORY(MatchBuilderLog)

// Infrastructure layer
Q_DECLARE_LOGGING_CATEGORY(TextInputLog)
Q_DECLARE_LOGGING_CATEGORY(DBusNotificationLog)

} // namespace Runner
} // namespace YubiKeyOath
