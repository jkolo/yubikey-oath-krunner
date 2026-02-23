/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

// Daemon service
Q_LOGGING_CATEGORY(OathDaemonLog, "pl.jkolo.yubikey.oath.daemon.daemon", QtWarningMsg)

// OATH components
Q_LOGGING_CATEGORY(OathDeviceManagerLog, "pl.jkolo.yubikey.oath.daemon.manager", QtWarningMsg)
Q_LOGGING_CATEGORY(YubiKeyOathDeviceLog, "pl.jkolo.yubikey.oath.daemon.oath.device", QtWarningMsg)

// PC/SC components
Q_LOGGING_CATEGORY(YubiKeyPcscLog, "pl.jkolo.yubikey.oath.daemon.pcsc.transaction", QtWarningMsg)
Q_LOGGING_CATEGORY(CardReaderMonitorLog, "pl.jkolo.yubikey.oath.daemon.pcsc", QtWarningMsg)

// Storage components
Q_LOGGING_CATEGORY(SecretStorageLog, "pl.jkolo.yubikey.oath.daemon.storage", QtWarningMsg)
Q_LOGGING_CATEGORY(OathDatabaseLog, "pl.jkolo.yubikey.oath.daemon.database", QtWarningMsg)

// Action and workflow components (moved from krunner)
Q_LOGGING_CATEGORY(OathActionCoordinatorLog, "pl.jkolo.yubikey.oath.daemon.actioncoordinator", QtWarningMsg)
Q_LOGGING_CATEGORY(ActionExecutorLog, "pl.jkolo.yubikey.oath.daemon.action", QtWarningMsg)
Q_LOGGING_CATEGORY(NotificationOrchestratorLog, "pl.jkolo.yubikey.oath.daemon.notification", QtWarningMsg)
Q_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog, "pl.jkolo.yubikey.oath.daemon.touch", QtWarningMsg)
Q_LOGGING_CATEGORY(TextInputLog, "pl.jkolo.yubikey.oath.daemon.input", QtWarningMsg)
Q_LOGGING_CATEGORY(DBusNotificationLog, "pl.jkolo.yubikey.oath.daemon.dbus", QtWarningMsg)

// Utility components
Q_LOGGING_CATEGORY(ScreenshotCaptureLog, "pl.jkolo.yubikey.oath.daemon.screenshot", QtWarningMsg)
Q_LOGGING_CATEGORY(QrCodeParserLog, "pl.jkolo.yubikey.oath.daemon.qr", QtWarningMsg)

} // namespace Daemon
} // namespace YubiKeyOath
