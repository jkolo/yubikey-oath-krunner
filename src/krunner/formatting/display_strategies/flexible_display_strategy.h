/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../../shared/types/oath_credential.h"
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Flexible credential display formatting strategy
 *
 * Provides customizable formatting based on user preferences.
 * Supports showing/hiding username, code, and device name.
 *
 * @par Example Formats
 * - Minimal: "Google"
 * - With username: "Google (user@example.com)"
 * - With code: "Google (user@example.com) - 123456"
 * - With device: "Google (user@example.com) - 123456 @ YubiKey 5"
 */
class FlexibleDisplayStrategy
{
public:
    /**
     * @brief Formats credential for display with flexible options
     *
     * @param credential Credential to format
     * @param showUsername Whether to show username in parentheses
     * @param showCode Whether to show TOTP/HOTP code (only if not touch-required)
     * @param showDeviceName Whether to show device name
     * @param deviceName Name of the YubiKey device (e.g., "YubiKey 5")
     * @param connectedDeviceCount Number of currently connected devices
     * @param showDeviceOnlyWhenMultiple If true, only show device name when connectedDeviceCount > 1
     * @return Formatted display string
     *
     * @note Thread-safe: Can be called from any thread
     * @note For touch-required credentials, code will never be shown even if showCode is true
     */
    static QString format(const OathCredential &credential,
                          bool showUsername,
                          bool showCode,
                          bool showDeviceName,
                          const QString &deviceName,
                          int connectedDeviceCount,
                          bool showDeviceOnlyWhenMultiple);

    /**
     * @brief Formats credential with code status indicator
     *
     * Similar to format(), but handles explicit code and touch status.
     * Used when we already generated the code or know touch is required.
     *
     * @param credential Credential to format
     * @param code Generated TOTP/HOTP code (may be empty)
     * @param requiresTouch Whether credential requires physical touch
     * @param showUsername Whether to show username in parentheses
     * @param showCode Whether to show code (only if provided and not touch-required)
     * @param showDeviceName Whether to show device name
     * @param deviceName Name of the YubiKey device
     * @param connectedDeviceCount Number of currently connected devices
     * @param showDeviceOnlyWhenMultiple Only show device name when multiple devices
     * @return Formatted display string
     */
    static QString formatWithCode(const OathCredential &credential,
                                   const QString &code,
                                   bool requiresTouch,
                                   bool showUsername,
                                   bool showCode,
                                   bool showDeviceName,
                                   const QString &deviceName,
                                   int connectedDeviceCount,
                                   bool showDeviceOnlyWhenMultiple);
};

} // namespace YubiKey
} // namespace KRunner
