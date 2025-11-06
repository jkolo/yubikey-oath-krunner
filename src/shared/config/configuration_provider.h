/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Pure interface for accessing plugin configuration
 *
 * Single Responsibility: Provide read-only access to configuration settings
 * Interface Segregation: Clients depend only on configuration access, not implementation
 * Dependency Inversion: Components depend on abstraction, not concrete KConfig
 *
 * @note This is a pure C++ interface (no QObject inheritance)
 * @note Concrete implementations (KRunnerConfiguration, DaemonConfiguration)
 *       inherit from both QObject and ConfigurationProvider to provide
 *       Qt signal support for configuration change notifications
 */
class ConfigurationProvider
{
public:
    virtual ~ConfigurationProvider() = default;

    /**
     * @brief Reloads configuration from storage
     */
    virtual void reload() = 0;

    /**
     * @brief Gets notification display preference
     * @return true if notifications should be shown
     */
    virtual bool showNotifications() const = 0;

    /**
     * @brief Gets username display preference
     * @return true if username should be shown in credential display
     */
    virtual bool showUsername() const = 0;

    /**
     * @brief Gets code display preference
     * @return true if TOTP/HOTP code should be shown (when not touch-required)
     */
    virtual bool showCode() const = 0;

    /**
     * @brief Gets device name display preference
     * @return true if device name should be shown in credential display
     */
    virtual bool showDeviceName() const = 0;

    /**
     * @brief Gets device name conditional display setting
     * @return true if device name should only be shown when multiple devices connected
     */
    virtual bool showDeviceNameOnlyWhenMultiple() const = 0;

    /**
     * @brief Gets touch timeout setting
     * @return Timeout in seconds
     */
    virtual int touchTimeout() const = 0;

    /**
     * @brief Gets notification extra time setting
     * @return Additional notification time in seconds
     */
    virtual int notificationExtraTime() const = 0;

    /**
     * @brief Gets primary action preference
     * @return Primary action ID ("copy" or "type")
     */
    virtual QString primaryAction() const = 0;

    /**
     * @brief Gets device reconnect timeout setting
     * @return Timeout in seconds for waiting for device reconnection
     */
    virtual int deviceReconnectTimeout() const = 0;
};

} // namespace Shared
} // namespace YubiKeyOath
