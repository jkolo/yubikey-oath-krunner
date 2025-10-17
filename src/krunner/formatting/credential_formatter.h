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
 * @brief Formats credential display names using flexible display options
 *
 * Single Responsibility: Handle credential display formatting
 * Wrapper around FlexibleDisplayStrategy for convenient formatting
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
     * @note Thread-safe
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
     * Overload for D-Bus CredentialInfo type.
     *
     * @param credential Credential to format (from D-Bus)
     * @param showUsername Whether to show username
     * @param showCode Whether to show code
     * @param showDeviceName Whether to show device name
     * @param deviceName Device name string
     * @param connectedDeviceCount Number of connected devices
     * @param showDeviceOnlyWhenMultiple Only show device name when multiple
     * @return Formatted string
     */
    static QString formatDisplayName(const CredentialInfo &credential,
                                      bool showUsername,
                                      bool showCode,
                                      bool showDeviceName,
                                      const QString &deviceName,
                                      int connectedDeviceCount,
                                      bool showDeviceOnlyWhenMultiple);
};

} // namespace YubiKey
} // namespace KRunner
