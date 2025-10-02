/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "configuration_provider.h"
#include <KConfigGroup>

namespace KRunner {
namespace YubiKey {

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
    QString displayFormat() const override;
    int touchTimeout() const override;
    int notificationExtraTime() const override;
    QString primaryAction() const override;

private:
    std::function<KConfigGroup()> m_configGroup;

    // Configuration keys
    static constexpr const char *CONFIG_SHOW_NOTIFICATIONS = "ShowNotifications";
    static constexpr const char *CONFIG_DISPLAY_FORMAT = "DisplayFormat";
    static constexpr const char *CONFIG_TOUCH_TIMEOUT = "TouchTimeout";
    static constexpr const char *CONFIG_NOTIFICATION_EXTRA_TIME = "NotificationExtraTime";
    static constexpr const char *CONFIG_PRIMARY_ACTION = "PrimaryAction";
};

} // namespace YubiKey
} // namespace KRunner
