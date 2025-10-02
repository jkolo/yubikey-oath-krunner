/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "full_strategy.h"
#include "name_user_strategy.h"

#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

QString FullStrategy::format(const OathCredential &credential) const
{
    // Get base formatting: "Issuer (username)"
    NameUserStrategy baseStrategy;
    QString base = baseStrategy.format(credential);

    // If credential has a code, include it with dash separator
    if (!credential.code.isEmpty()) {
        return QStringLiteral("%1 - %2").arg(base, credential.code);
    }

    // No code - return base format only
    return base;
}

QString FullStrategy::formatWithCode(const OathCredential &credential, const QString &code, bool requiresTouch) const
{
    // Get base formatting: "Issuer (username)"
    NameUserStrategy baseStrategy;
    QString base = baseStrategy.format(credential);

    // Add code or touch status in brackets
    if (requiresTouch) {
        // Translatable "Touch Required" indicator
        return i18n("%1 [Touch Required]", base);
    } else if (!code.isEmpty()) {
        // Show actual code
        return QStringLiteral("%1 [%2]").arg(base, code);
    }

    // No code generated and not touch-required - return base format
    return base;
}

QString FullStrategy::identifier() const
{
    return QStringLiteral("full");
}

} // namespace YubiKey
} // namespace KRunner
