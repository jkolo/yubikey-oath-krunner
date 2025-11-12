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
    // Delegate to rich domain model method (Tell, Don't Ask principle)
    // This eliminates the anemic model anti-pattern where business logic
    // was scattered in utility classes instead of domain objects
    return credential.getDisplayName(options);
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
    // Delegate to rich domain model method (Tell, Don't Ask principle)
    return credential.getDisplayNameWithCode(code, requiresTouch, options);
}

} // namespace Shared
} // namespace YubiKeyOath
