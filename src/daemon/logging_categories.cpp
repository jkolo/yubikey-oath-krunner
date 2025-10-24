/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "logging_categories.h"

namespace KRunner {
namespace YubiKey {

// Daemon service
Q_LOGGING_CATEGORY(YubiKeyDaemonLog, "org.kde.plasma.krunner.yubikey.daemon", QtWarningMsg)

// OATH components
Q_LOGGING_CATEGORY(YubiKeyDeviceManagerLog, "org.kde.plasma.krunner.yubikey.manager", QtWarningMsg)
Q_LOGGING_CATEGORY(YubiKeyOathDeviceLog, "org.kde.plasma.krunner.yubikey.oath.device", QtWarningMsg)

// Storage components
Q_LOGGING_CATEGORY(CardReaderMonitorLog, "org.kde.plasma.krunner.yubikey.pcsc", QtWarningMsg)
Q_LOGGING_CATEGORY(PasswordStorageLog, "org.kde.plasma.krunner.yubikey.storage", QtWarningMsg)
Q_LOGGING_CATEGORY(YubiKeyDatabaseLog, "org.kde.plasma.krunner.yubikey.database", QtWarningMsg)

// Action and workflow components (moved from krunner)
Q_LOGGING_CATEGORY(YubiKeyActionCoordinatorLog, "org.kde.plasma.krunner.yubikey.actioncoordinator", QtWarningMsg)
Q_LOGGING_CATEGORY(ActionExecutorLog, "org.kde.plasma.krunner.yubikey.action", QtWarningMsg)
Q_LOGGING_CATEGORY(NotificationOrchestratorLog, "org.kde.plasma.krunner.yubikey.notification", QtWarningMsg)
Q_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog, "org.kde.plasma.krunner.yubikey.touch", QtWarningMsg)
Q_LOGGING_CATEGORY(TextInputLog, "org.kde.plasma.krunner.yubikey.input", QtWarningMsg)
Q_LOGGING_CATEGORY(DBusNotificationLog, "org.kde.plasma.krunner.yubikey.dbus", QtWarningMsg)

} // namespace YubiKey
} // namespace KRunner
