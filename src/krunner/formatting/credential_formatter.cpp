/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_formatter.h"
#include "display_strategies/flexible_display_strategy.h"

namespace KRunner {
namespace YubiKey {

QString CredentialFormatter::formatDisplayName(const OathCredential &credential,
                                                bool showUsername,
                                                bool showCode,
                                                bool showDeviceName,
                                                const QString &deviceName,
                                                int connectedDeviceCount,
                                                bool showDeviceOnlyWhenMultiple)
{
    return FlexibleDisplayStrategy::format(credential,
                                            showUsername,
                                            showCode,
                                            showDeviceName,
                                            deviceName,
                                            connectedDeviceCount,
                                            showDeviceOnlyWhenMultiple);
}

QString CredentialFormatter::formatDisplayName(const CredentialInfo &credential,
                                                bool showUsername,
                                                bool showCode,
                                                bool showDeviceName,
                                                const QString &deviceName,
                                                int connectedDeviceCount,
                                                bool showDeviceOnlyWhenMultiple)
{
    // Convert CredentialInfo to OathCredential for formatting
    OathCredential oathCred;
    oathCred.name = credential.name;
    oathCred.issuer = credential.issuer;
    oathCred.username = credential.username;
    oathCred.requiresTouch = credential.requiresTouch;
    oathCred.isTotp = true; // Default to TOTP (daemon doesn't distinguish)
    oathCred.code = QString(); // No code in CredentialInfo
    oathCred.deviceId = credential.deviceId;

    return formatDisplayName(oathCred,
                             showUsername,
                             showCode,
                             showDeviceName,
                             deviceName,
                             connectedDeviceCount,
                             showDeviceOnlyWhenMultiple);
}

} // namespace YubiKey
} // namespace KRunner
