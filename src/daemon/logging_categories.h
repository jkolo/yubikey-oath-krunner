/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QLoggingCategory>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Qt Logging Categories for YubiKey OATH Daemon
 *
 * Control via environment:
 *   QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true"
 */

// Daemon service
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDaemonLog)

// OATH components
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDeviceManagerLog)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyOathDeviceLog)

// Storage components
Q_DECLARE_LOGGING_CATEGORY(CardReaderMonitorLog)
Q_DECLARE_LOGGING_CATEGORY(PasswordStorageLog)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDatabaseLog)

// Action and workflow components (moved from krunner)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyActionCoordinatorLog)
Q_DECLARE_LOGGING_CATEGORY(ActionExecutorLog)
Q_DECLARE_LOGGING_CATEGORY(NotificationOrchestratorLog)
Q_DECLARE_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog)
Q_DECLARE_LOGGING_CATEGORY(TextInputLog)
Q_DECLARE_LOGGING_CATEGORY(DBusNotificationLog)

} // namespace YubiKey
} // namespace KRunner
