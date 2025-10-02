/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "name_user_strategy.h"

namespace KRunner {
namespace YubiKey {

QString NameUserStrategy::format(const OathCredential &credential) const
{
    if (credential.issuer.isEmpty()) {
        return credential.username;
    } else if (credential.username.isEmpty()) {
        return credential.issuer;
    } else {
        return QStringLiteral("%1 (%2)").arg(credential.issuer, credential.username);
    }
}

QString NameUserStrategy::identifier() const
{
    return QStringLiteral("name_user");
}

} // namespace YubiKey
} // namespace KRunner
