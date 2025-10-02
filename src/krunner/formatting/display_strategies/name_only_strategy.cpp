/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "name_only_strategy.h"

namespace KRunner {
namespace YubiKey {

QString NameOnlyStrategy::format(const OathCredential &credential) const
{
    return credential.issuer.isEmpty() ? credential.username : credential.issuer;
}

QString NameOnlyStrategy::identifier() const
{
    return QStringLiteral("name");
}

} // namespace YubiKey
} // namespace KRunner
