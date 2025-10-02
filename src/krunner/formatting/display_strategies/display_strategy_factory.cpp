/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "display_strategy_factory.h"
#include "name_only_strategy.h"
#include "name_user_strategy.h"
#include "full_strategy.h"

namespace KRunner {
namespace YubiKey {

std::unique_ptr<IDisplayStrategy> DisplayStrategyFactory::createStrategy(const QString &identifier)
{
    if (identifier == QStringLiteral("name")) {
        return std::make_unique<NameOnlyStrategy>();
    } else if (identifier == QStringLiteral("name_user")) {
        return std::make_unique<NameUserStrategy>();
    } else if (identifier == QStringLiteral("full")) {
        return std::make_unique<FullStrategy>();
    }

    // Default: NameUserStrategy
    return std::make_unique<NameUserStrategy>();
}

QString DisplayStrategyFactory::defaultIdentifier()
{
    return QStringLiteral("name_user");
}

} // namespace YubiKey
} // namespace KRunner
