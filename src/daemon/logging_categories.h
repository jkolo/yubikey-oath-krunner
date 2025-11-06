/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QLoggingCategory>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Qt Logging Categories for YubiKey OATH Daemon
 *
 * Control via environment:
 *   QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true"
 */

// Daemon service
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDaemonLog)

// OATH components
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDeviceManagerLog)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyOathDeviceLog)

// Storage components
Q_DECLARE_LOGGING_CATEGORY(CardReaderMonitorLog)
Q_DECLARE_LOGGING_CATEGORY(SecretStorageLog)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyDatabaseLog)

// Action and workflow components (moved from krunner)
Q_DECLARE_LOGGING_CATEGORY(YubiKeyActionCoordinatorLog)
Q_DECLARE_LOGGING_CATEGORY(ActionExecutorLog)
Q_DECLARE_LOGGING_CATEGORY(NotificationOrchestratorLog)
Q_DECLARE_LOGGING_CATEGORY(TouchWorkflowCoordinatorLog)
Q_DECLARE_LOGGING_CATEGORY(TextInputLog)
Q_DECLARE_LOGGING_CATEGORY(DBusNotificationLog)

// Utility components
Q_DECLARE_LOGGING_CATEGORY(ScreenshotCaptureLog)
Q_DECLARE_LOGGING_CATEGORY(QrCodeParserLog)

} // namespace Daemon
} // namespace YubiKeyOath
