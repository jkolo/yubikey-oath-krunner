/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../shared/types/oath_credential.h"
#include "../../shared/dbus/yubikey_dbus_types.h"
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Formats credential display names with flexible display options
 *
 * Single Responsibility: Handle credential display formatting
 * Provides customizable formatting based on user preferences.
 * Supports showing/hiding username, code, and device name.
 *
 * @par Example Formats
 * - Minimal: "Google"
 * - With username: "Google (user@example.com)"
 * - With code: "Google (user@example.com) - 123456"
 * - Touch required: "Google (user@example.com) - 👆"
 * - With device: "Google (user@example.com) - 123456 @ YubiKey 5"
 */
class CredentialFormatter
{
public:
    /**
     * @brief Formats credential for display with flexible options
     *
     * @param credential Credential to format
     * @param showUsername Whether to show username in parentheses
     * @param showCode Whether to show TOTP/HOTP code (only if not touch-required)
     * @param showDeviceName Whether to show device name
     * @param deviceName Name of the YubiKey device
     * @param connectedDeviceCount Number of currently connected devices
     * @param showDeviceOnlyWhenMultiple Only show device name when multiple devices
     * @return Formatted string
     *
     * @note Thread-safe: Can be called from any thread
     * @note For touch-required credentials, code will never be shown even if showCode is true
     */
    static QString formatDisplayName(const OathCredential &credential,
                                      bool showUsername,
                                      bool showCode,
                                      bool showDeviceName,
                                      const QString &deviceName,
                                      int connectedDeviceCount,
                                      bool showDeviceOnlyWhenMultiple);

    /**
     * @brief Formats CredentialInfo for display with flexible options
     *
     * Overload for D-Bus CredentialInfo type. Converts to OathCredential internally.
     *
     * @param credential Credential to format (from D-Bus)
     * @param showUsername Whether to show username
     * @param showCode Whether to show code
     * @param showDeviceName Whether to show device name
     * @param deviceName Device name string
     * @param connectedDeviceCount Number of connected devices
     * @param showDeviceOnlyWhenMultiple Only show device name when multiple
     * @return Formatted string
     *
     * @note Thread-safe
     */
    static QString formatDisplayName(const CredentialInfo &credential,
                                      bool showUsername,
                                      bool showCode,
                                      bool showDeviceName,
                                      const QString &deviceName,
                                      int connectedDeviceCount,
                                      bool showDeviceOnlyWhenMultiple);

    /**
     * @brief Formats credential with explicit code and touch status
     *
     * Similar to formatDisplayName(), but handles explicit code and touch status.
     * Used when we already generated the code or know touch is required.
     * This allows passing a code separately from the credential object.
     *
     * @param credential Credential to format
     * @param code Generated TOTP/HOTP code (may be empty)
     * @param requiresTouch Whether credential requires physical touch
     * @param showUsername Whether to show username in parentheses
     * @param showCode Whether to show code/touch indicator
     * @param showDeviceName Whether to show device name
     * @param deviceName Name of the YubiKey device
     * @param connectedDeviceCount Number of currently connected devices
     * @param showDeviceOnlyWhenMultiple Only show device name when multiple devices
     * @return Formatted display string
     *
     * @note Thread-safe
     * @note When showCode=true and requiresTouch=true, displays 👆 emoji
     * @note When showCode=true and requiresTouch=false and code is not empty, displays the code
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
