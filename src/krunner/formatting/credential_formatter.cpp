/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_formatter.h"
#include "display_strategies/display_strategy_factory.h"

namespace KRunner {
namespace YubiKey {

// Primary method: Uses Strategy Pattern
QString CredentialFormatter::formatDisplayName(const OathCredential &credential, const QString &identifier)
{
    auto strategy = DisplayStrategyFactory::createStrategy(identifier);
    return strategy->format(credential);
}

// Overload for CredentialInfo (D-Bus type)
QString CredentialFormatter::formatDisplayName(const CredentialInfo &credential, const QString &identifier)
{
    // Convert CredentialInfo to OathCredential for formatting
    OathCredential oathCred;
    oathCred.name = credential.name;
    oathCred.issuer = credential.issuer;
    oathCred.username = credential.username;
    oathCred.requiresTouch = credential.requiresTouch;
    oathCred.isTotp = true; // Default to TOTP (daemon doesn't distinguish)

    return formatDisplayName(oathCred, identifier);
}

// Overload for CredentialInfo with enum format
QString CredentialFormatter::formatDisplayName(const CredentialInfo &credential, DisplayFormat format)
{
    // Convert CredentialInfo to OathCredential for formatting
    OathCredential oathCred;
    oathCred.name = credential.name;
    oathCred.issuer = credential.issuer;
    oathCred.username = credential.username;
    oathCred.requiresTouch = credential.requiresTouch;
    oathCred.isTotp = true; // Default to TOTP (daemon doesn't distinguish)

    return formatDisplayName(oathCred, format);
}

// Deprecated: Backward compatibility wrapper
QString CredentialFormatter::formatDisplayName(const OathCredential &credential, DisplayFormat format)
{
    QString identifier;
    switch (format) {
    case Name:
        identifier = QStringLiteral("name");
        break;
    case NameUser:
        identifier = QStringLiteral("name_user");
        break;
    case Full:
        identifier = QStringLiteral("full");
        break;
    default:
        identifier = QStringLiteral("name_user");
        break;
    }

    return formatDisplayName(credential, identifier);
}

// Deprecated: Backward compatibility only
CredentialFormatter::DisplayFormat CredentialFormatter::formatFromString(const QString &formatString)
{
    if (formatString == QStringLiteral("name")) {
        return Name;
    } else if (formatString == QStringLiteral("name_user")) {
        return NameUser;
    } else if (formatString == QStringLiteral("full")) {
        return Full;
    }

    return NameUser; // Default
}

QString CredentialFormatter::defaultFormat()
{
    return DisplayStrategyFactory::defaultIdentifier();
}

} // namespace YubiKey
} // namespace KRunner
