/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_display_strategy.h"
#include <memory>
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Factory for creating display strategy instances
 *
 * Factory Method Pattern: Centralizes creation of strategy objects
 * Open/Closed Principle: New strategies can be added by extending factory
 * Single Responsibility: Only responsible for strategy creation
 *
 * @par Usage Example
 * @code
 * // Create strategy from configuration
 * QString formatId = config->displayFormat(); // "name_user"
 * auto strategy = DisplayStrategyFactory::createStrategy(formatId);
 * QString display = strategy->format(credential);
 * @endcode
 *
 * @par Supported Strategies
 * - "name" → NameOnlyStrategy
 * - "name_user" → NameUserStrategy (default)
 * - "full" → FullStrategy
 */
class DisplayStrategyFactory
{
public:
    /**
     * @brief Creates display strategy based on identifier
     *
     * @param identifier Strategy identifier ("name", "name_user", "full")
     * @return unique_ptr to strategy instance, never null (defaults to NameUserStrategy)
     *
     * @note Thread-safe: Can be called from any thread
     * @note Returns NameUserStrategy as default for unknown identifiers
     *
     * @par Memory Management
     * Returns std::unique_ptr for automatic cleanup. Caller owns the strategy.
     */
    static std::unique_ptr<IDisplayStrategy> createStrategy(const QString &identifier);

    /**
     * @brief Gets default strategy identifier
     * @return "name_user"
     */
    static QString defaultIdentifier();
};

} // namespace YubiKey
} // namespace KRunner
