/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "text_input_provider.h"
#include <memory>

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class SecretStorage;

/**
 * @brief Factory for creating appropriate text input provider
 *
 * Factory Pattern: Creates appropriate implementation based on environment
 */
class TextInputFactory
{
public:
    /**
     * @brief Creates text input provider for current session
     * @param secretStorage Secret storage for KWallet operations (token persistence)
     * @param parent Parent QObject
     * @return Compatible text input provider or nullptr
     */
    static std::unique_ptr<TextInputProvider> createProvider(SecretStorage *secretStorage,
                                                             QObject *parent = nullptr);
};

} // namespace Daemon
} // namespace YubiKeyOath
