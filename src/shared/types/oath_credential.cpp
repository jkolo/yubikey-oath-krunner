/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_credential.h"
#include "../formatting/credential_formatter.h" // For FormatOptions
#include <QDateTime>

namespace YubiKeyOath {
namespace Shared {

QString OathCredential::getDisplayName(const FormatOptions &options) const
{
    // Start with issuer (or account if no issuer)
    QString result = issuer.isEmpty() ? account : issuer;

    // Add account if requested
    if (options.showUsername && !account.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(account);
    }

    // Add code if requested and available (only for non-touch credentials)
    if (options.showCode && !requiresTouch && !code.isEmpty()) {
        result += QStringLiteral(" - %1").arg(code);
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

QString OathCredential::getDisplayNameWithCode(const QString &explicitCode,
                                                 bool explicitRequiresTouch,
                                                 const FormatOptions &options) const
{
    // Start with issuer (or account if no issuer)
    QString result = issuer.isEmpty() ? account : issuer;

    // Add account if requested
    if (options.showUsername && !account.isEmpty()) {
        result += QStringLiteral(" (%1)").arg(account);
    }

    // Add code or touch indicator if requested
    if (options.showCode) {
        if (explicitRequiresTouch) {
            // Show touch required emoji
            result += QStringLiteral(" 👆");
        } else if (!explicitCode.isEmpty()) {
            // Show actual code
            result += QStringLiteral(" - %1").arg(explicitCode);
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

bool OathCredential::matches(const QString &name, const QString &targetDeviceId) const
{
    // Exact name comparison (case-sensitive)
    return originalName == name && deviceId == targetDeviceId;
}

bool OathCredential::isExpired() const
{
    // HOTP credentials don't expire
    if (!isTotp) {
        return false;
    }

    // Check if validUntil timestamp is in the past
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return validUntil > 0 && now >= validUntil;
}

bool OathCredential::needsRegeneration(int thresholdSeconds) const
{
    // HOTP credentials don't need regeneration based on time
    if (!isTotp) {
        return false;
    }

    // Check if code will expire within threshold
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return validUntil > 0 && (validUntil - now) <= thresholdSeconds;
}

} // namespace Shared
} // namespace YubiKeyOath
