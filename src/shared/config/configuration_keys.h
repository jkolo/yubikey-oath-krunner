/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

namespace YubiKeyOath {
namespace Shared {
namespace ConfigKeys {

/**
 * @brief Shared configuration key constants
 *
 * These constants define the keys used in KConfig for storing YubiKey plugin settings.
 * They are shared between daemon and krunner configurations to ensure consistency.
 *
 * IMPORTANT: Do not change these values as they are persisted in user configuration files.
 */

// Notification settings
constexpr const char *SHOW_NOTIFICATIONS = "ShowNotifications";
constexpr const char *NOTIFICATION_EXTRA_TIME = "NotificationExtraTime";

// Display settings
constexpr const char *SHOW_USERNAME = "ShowUsername";
constexpr const char *SHOW_CODE = "ShowCode";
constexpr const char *SHOW_DEVICE_NAME = "ShowDeviceName";
constexpr const char *SHOW_DEVICE_NAME_ONLY_WHEN_MULTIPLE = "ShowDeviceNameOnlyWhenMultiple";

// Behavior settings
constexpr const char *TOUCH_TIMEOUT = "TouchTimeout";
constexpr const char *PRIMARY_ACTION = "PrimaryAction";

// Caching settings
constexpr const char *ENABLE_CREDENTIALS_CACHE = "EnableCredentialsCache";
constexpr const char *DEVICE_RECONNECT_TIMEOUT = "DeviceReconnectTimeout";
constexpr const char *CREDENTIAL_SAVE_RATE_LIMIT_MS = "CredentialSaveRateLimit";

} // namespace ConfigKeys
} // namespace Shared
} // namespace YubiKeyOath
