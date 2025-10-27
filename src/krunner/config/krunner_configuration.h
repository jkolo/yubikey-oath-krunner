/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "config/configuration_provider.h"
#include "config/configuration_keys.h"
#include <KConfigGroup>

namespace YubiKeyOath {
namespace Runner {
using Shared::ConfigurationProvider;

/**
 * @brief KRunner-specific implementation of ConfigurationProvider
 *
 * Single Responsibility: Adapt KConfig to ConfigurationProvider interface
 * Adapter Pattern: Adapts KRunner::AbstractRunner::config() to our interface
 */
class KRunnerConfiguration : public ConfigurationProvider
{
public:
    /**
     * @brief Constructs configuration provider from KConfigGroup
     * @param configGroup Function to get current config group
     * @param parent Parent QObject
     */
    explicit KRunnerConfiguration(std::function<KConfigGroup()> configGroup, QObject *parent = nullptr);

    bool showNotifications() const override;
    bool showUsername() const override;
    bool showCode() const override;
    bool showDeviceName() const override;
    bool showDeviceNameOnlyWhenMultiple() const override;
    int touchTimeout() const override;
    int notificationExtraTime() const override;
    QString primaryAction() const override;

private:
    std::function<KConfigGroup()> m_configGroup;

    // Configuration keys now defined in shared/config/configuration_keys.h

    /**
     * @brief Template helper for reading config entries
     * Reduces boilerplate in configuration reading methods
     */
    template<typename T>
    T readConfigEntry(const char* key, const T& defaultValue) const {
        return m_configGroup().readEntry(key, defaultValue);
    }
};

} // namespace Runner
} // namespace YubiKeyOath
