/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_formatter.h"

namespace YubiKeyOath {
namespace Shared {

QString CredentialFormatter::formatDisplayName(const OathCredential &credential,
                                                const FormatOptions &options)
{
    // Start with issuer (or account if no issuer)
    QString result = credential.issuer.isEmpty() ? credential.account : credential.issuer;

    // Add account if requested
    if (options.showUsername && !credential.account.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(credential.account);
    }

    // Add code if requested and available (only for non-touch credentials)
    if (options.showCode && !credential.requiresTouch && !credential.code.isEmpty()) {
        result += QStringLiteral(" - %1").arg(credential.code);
    }

    // Add device name if requested
    if (options.showDeviceName && !options.deviceName.isEmpty()) {
        // Check if we should only show when multiple devices
        const bool shouldShowDevice = !options.showDeviceOnlyWhenMultiple || options.connectedDeviceCount > 1;

        if (shouldShowDevice) {
            result += QStringLiteral(" @ %1").arg(options.deviceName);
        }
    }

    return result;
}

QString CredentialFormatter::formatDisplayName(const CredentialInfo &credential,
                                                const FormatOptions &options)
{
    // Convert CredentialInfo to OathCredential for formatting
    OathCredential oathCred;
    oathCred.originalName = credential.name;
    oathCred.issuer = credential.issuer;
    oathCred.account = credential.account;
    oathCred.requiresTouch = credential.requiresTouch;
    oathCred.isTotp = true; // Default to TOTP (daemon doesn't distinguish)
    oathCred.code = QString(); // No code in CredentialInfo
    oathCred.deviceId = credential.deviceId;

    return formatDisplayName(oathCred, options);
}

QString CredentialFormatter::formatWithCode(const OathCredential &credential,
                                             const QString &code,
                                             bool requiresTouch,
                                             const FormatOptions &options)
{
    // Start with issuer (or account if no issuer)
    QString result = credential.issuer.isEmpty() ? credential.account : credential.issuer;

    // Add account if requested
    if (options.showUsername && !credential.account.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(credential.account);
    }

    // Add code or touch indicator if requested
    if (options.showCode) {
        if (requiresTouch) {
            // Show touch required emoji
            result += QStringLiteral(" 👆");
        } else if (!code.isEmpty()) {
            // Show actual code
            result += QStringLiteral(" - %1").arg(code);
        }
    }

    // Add device name if requested
    if (options.showDeviceName && !options.deviceName.isEmpty()) {
        // Check if we should only show when multiple devices
        const bool shouldShowDevice = !options.showDeviceOnlyWhenMultiple || options.connectedDeviceCount > 1;

        if (shouldShowDevice) {
            result += QStringLiteral(" @ %1").arg(options.deviceName);
        }
    }

    return result;
}

} // namespace Shared
} // namespace YubiKeyOath
