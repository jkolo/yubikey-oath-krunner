/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Pure interface for text input providers
 *
 * Interface Segregation Principle: Dedicated interface for text typing
 * Open/Closed Principle: New input methods can be added without modification
 *
 * @note This is a pure C++ interface (no QObject inheritance)
 * @note Concrete implementations may inherit from QObject if they need Qt features
 */
class TextInputProvider
{
public:
    virtual ~TextInputProvider() = default;

    /**
     * @brief Types text into active window
     * @param text Text to type
     * @return true if successful
     */
    virtual bool typeText(const QString &text) = 0;

    /**
     * @brief Checks if this provider can handle current session
     * @return true if provider is compatible
     */
    virtual bool isCompatible() const = 0;

    /**
     * @brief Gets provider name for logging/debugging
     * @return Provider name
     */
    virtual QString providerName() const = 0;

    /**
     * @brief Check if last typeText() failure was due to waiting for permission
     * @return true if permission dialog timeout occurred
     */
    virtual bool isWaitingForPermission() const { return false; }

    /**
     * @brief Check if user explicitly rejected permission request
     * @return true if permission was rejected (not just timeout)
     */
    virtual bool wasPermissionRejected() const { return false; }
};

} // namespace Daemon
} // namespace YubiKeyOath
