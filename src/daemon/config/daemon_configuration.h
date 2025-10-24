/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "config/configuration_provider.h"
#include "config/configuration_keys.h"
#include <QString>
#include <KSharedConfig>
#include <KConfigGroup>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Configuration reader for daemon
 *
 * Reads settings from krunnerrc file for daemon operations
 */
class DaemonConfiguration : public ConfigurationProvider
{
    Q_OBJECT

public:
    explicit DaemonConfiguration(QObject *parent = nullptr);

    /**
     * @brief Reloads configuration from file
     */
    void reload();

    bool showNotifications() const override;
    bool showUsername() const override;
    bool showCode() const override;
    bool showDeviceName() const override;
    bool showDeviceNameOnlyWhenMultiple() const override;
    int touchTimeout() const override;
    int notificationExtraTime() const override;
    QString primaryAction() const override;

private:
    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;

    // Configuration keys now defined in shared/config/configuration_keys.h

    template<typename T>
    T readConfigEntry(const char* key, const T& defaultValue) const {
        return m_configGroup.readEntry(key, defaultValue);
    }
};

} // namespace YubiKey
} // namespace KRunner
