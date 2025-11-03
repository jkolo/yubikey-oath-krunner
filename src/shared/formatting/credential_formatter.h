/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../types/oath_credential.h"
#include "../types/yubikey_value_types.h"
#include <QString>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Options for credential display formatting
 *
 * Encapsulates all formatting options to reduce parameter count.
 * Used by CredentialFormatter methods.
 *
 * @par Preferred Usage
 * Use FormatOptionsBuilder for readable, self-documenting code:
 * @code
 * FormatOptions options = FormatOptionsBuilder()
 *     .withUsername()
 *     .withDevice("YubiKey 5")
 *     .withDeviceCount(2)
 *     .onlyWhenMultipleDevices()
 *     .build();
 * @endcode
 *
 * @par Legacy Usage
 * Direct constructor is also available for backward compatibility.
 */
struct FormatOptions {
    bool showUsername = false;              ///< Show username in parentheses
    bool showCode = false;                  ///< Show TOTP/HOTP code (if available)
    bool showDeviceName = false;            ///< Show device name
    QString deviceName;                     ///< Name of the YubiKey device
    int connectedDeviceCount = 0;           ///< Number of currently connected devices
    bool showDeviceOnlyWhenMultiple = false; ///< Only show device name when multiple devices

    /**
     * @brief Constructs FormatOptions with all parameters
     * @deprecated Use FormatOptionsBuilder for better readability
     */
    FormatOptions(bool username = false,
                  bool code = false,
                  bool device = false,
                  const QString &devName = QString(),
                  int deviceCount = 0,
                  bool deviceOnlyMultiple = false)
        : showUsername(username)
        , showCode(code)
        , showDeviceName(device)
        , deviceName(devName)
        , connectedDeviceCount(deviceCount)
        , showDeviceOnlyWhenMultiple(deviceOnlyMultiple)
    {}

};

/**
 * @brief Builder for FormatOptions with fluent API
 *
 * Provides a readable, self-documenting way to construct FormatOptions.
 * Improves code clarity by making each option explicit and named.
 *
 * @par Example Usage
 * @code
 * // Minimal configuration
 * auto options = FormatOptionsBuilder().build();
 *
 * // With username only
 * auto options = FormatOptionsBuilder()
 *     .withUsername()
 *     .build();
 *
 * // Full configuration
 * auto options = FormatOptionsBuilder()
 *     .withUsername()
 *     .withCode()
 *     .withDevice("YubiKey 5 NFC")
 *     .withDeviceCount(3)
 *     .onlyWhenMultipleDevices()
 *     .build();
 *
 * // Conditional configuration
 * auto builder = FormatOptionsBuilder();
 * if (config->showUsername()) {
 *     builder.withUsername();
 * }
 * if (config->showDeviceName()) {
 *     builder.withDevice(deviceName);
 * }
 * auto options = builder.build();
 * @endcode
 *
 * @note Thread-safe: Builder is not thread-safe, but FormatOptions is
 * @note All methods return *this for method chaining
 */
class FormatOptionsBuilder {
public:
    /**
     * @brief Constructs builder with default FormatOptions
     */
    FormatOptionsBuilder() : m_options() {}

    /**
     * @brief Show username in parentheses
     * @param show Enable/disable username display (default: true)
     * @return Reference to this builder for chaining
     */
    FormatOptionsBuilder& withUsername(bool show = true) {
        m_options.showUsername = show;
        return *this;
    }

    /**
     * @brief Show TOTP/HOTP code if available
     * @param show Enable/disable code display (default: true)
     * @return Reference to this builder for chaining
     */
    FormatOptionsBuilder& withCode(bool show = true) {
        m_options.showCode = show;
        return *this;
    }

    /**
     * @brief Show device name
     * @param deviceName Name of the YubiKey device
     * @param show Enable/disable device name display (default: true)
     * @return Reference to this builder for chaining
     *
     * @note If deviceName is empty, device name will not be shown
     */
    FormatOptionsBuilder& withDevice(const QString& deviceName, bool show = true) {
        m_options.deviceName = deviceName;
        m_options.showDeviceName = show && !deviceName.isEmpty();
        return *this;
    }

    /**
     * @brief Set number of connected devices
     * @param count Number of currently connected devices
     * @return Reference to this builder for chaining
     *
     * @note Used with onlyWhenMultipleDevices() to conditionally show device name
     */
    FormatOptionsBuilder& withDeviceCount(int count) {
        m_options.connectedDeviceCount = count;
        return *this;
    }

    /**
     * @brief Only show device name when multiple devices are connected
     * @param enable Enable/disable this option (default: true)
     * @return Reference to this builder for chaining
     *
     * @note Requires withDeviceCount() to be set for proper behavior
     */
    FormatOptionsBuilder& onlyWhenMultipleDevices(bool enable = true) {
        m_options.showDeviceOnlyWhenMultiple = enable;
        return *this;
    }

    /**
     * @brief Build and return the FormatOptions
     * @return Configured FormatOptions instance
     *
     * @note Builder can be reused after build() to create similar configurations
     */
    FormatOptions build() const {
        return m_options;
    }

private:
    FormatOptions m_options;
};

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
     * @param options Formatting options (see FormatOptions)
     * @return Formatted string
     *
     * @note Thread-safe: Can be called from any thread
     * @note For touch-required credentials, code will never be shown even if showCode is true
     */
    static QString formatDisplayName(const OathCredential &credential,
                                      const FormatOptions &options);

    /**
     * @brief Formats CredentialInfo for display with flexible options
     *
     * Overload for D-Bus CredentialInfo type. Converts to OathCredential internally.
     *
     * @param credential Credential to format (from D-Bus)
     * @param options Formatting options (see FormatOptions)
     * @return Formatted string
     *
     * @note Thread-safe
     */
    static QString formatDisplayName(const CredentialInfo &credential,
                                      const FormatOptions &options);

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
     * @param options Formatting options (see FormatOptions)
     * @return Formatted display string
     *
     * @note Thread-safe
     * @note When showCode=true and requiresTouch=true, displays 👆 emoji
     * @note When showCode=true and requiresTouch=false and code is not empty, displays the code
     */
    static QString formatWithCode(const OathCredential &credential,
                                   const QString &code,
                                   bool requiresTouch,
                                   const FormatOptions &options);
};

} // namespace Shared
} // namespace YubiKeyOath
