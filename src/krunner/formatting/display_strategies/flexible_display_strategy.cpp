/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "flexible_display_strategy.h"
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

QString FlexibleDisplayStrategy::format(const OathCredential &credential,
                                         bool showUsername,
                                         bool showCode,
                                         bool showDeviceName,
                                         const QString &deviceName,
                                         int connectedDeviceCount,
                                         bool showDeviceOnlyWhenMultiple)
{
    // Start with issuer (or full name if no issuer)
    QString result = credential.issuer.isEmpty() ? credential.name : credential.issuer;

    // Add username if requested
    if (showUsername && !credential.username.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(credential.username);
    }

    // Add code if requested and available (only for non-touch credentials)
    if (showCode && !credential.requiresTouch && !credential.code.isEmpty()) {
        result += QStringLiteral(" - %1").arg(credential.code);
    }

    // Add device name if requested
    if (showDeviceName && !deviceName.isEmpty()) {
        // Check if we should only show when multiple devices
        bool shouldShowDevice = !showDeviceOnlyWhenMultiple || connectedDeviceCount > 1;

        if (shouldShowDevice) {
            result += QStringLiteral(" @ %1").arg(deviceName);
        }
    }

    return result;
}

QString FlexibleDisplayStrategy::formatWithCode(const OathCredential &credential,
                                                 const QString &code,
                                                 bool requiresTouch,
                                                 bool showUsername,
                                                 bool showCode,
                                                 bool showDeviceName,
                                                 const QString &deviceName,
                                                 int connectedDeviceCount,
                                                 bool showDeviceOnlyWhenMultiple)
{
    // Start with issuer (or full name if no issuer)
    QString result = credential.issuer.isEmpty() ? credential.name : credential.issuer;

    // Add username if requested
    if (showUsername && !credential.username.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(credential.username);
    }

    // Add code or touch indicator if requested
    if (showCode) {
        if (requiresTouch) {
            // Show touch required indicator
            result += QStringLiteral(" [%1]").arg(i18n("Touch Required"));
        } else if (!code.isEmpty()) {
            // Show actual code
            result += QStringLiteral(" - %1").arg(code);
        }
    }

    // Add device name if requested
    if (showDeviceName && !deviceName.isEmpty()) {
        // Check if we should only show when multiple devices
        bool shouldShowDevice = !showDeviceOnlyWhenMultiple || connectedDeviceCount > 1;

        if (shouldShowDevice) {
            result += QStringLiteral(" @ %1").arg(deviceName);
        }
    }

    return result;
}

} // namespace YubiKey
} // namespace KRunner
