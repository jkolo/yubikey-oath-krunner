/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "config/configuration_provider.h"
#include "config/configuration_keys.h"
#include <KSharedConfig>
#include <KConfigGroup>
#include <QFileSystemWatcher>

namespace YubiKeyOath {
namespace Runner {
using Shared::ConfigurationProvider;

/**
 * @brief KRunner-specific implementation of ConfigurationProvider
 *
 * Single Responsibility: Reads settings from yubikey-oathrc file for KRunner operations
 * Note: Uses same config file as daemon (yubikey-oathrc) for consistency
 */
class KRunnerConfiguration : public ConfigurationProvider
{
public:
    /**
     * @brief Constructs configuration provider
     * @param parent Parent QObject
     */
    explicit KRunnerConfiguration(QObject *parent = nullptr);

    void reload() override;

    bool showNotifications() const override;
    bool showUsername() const override;
    bool showCode() const override;
    bool showDeviceName() const override;
    bool showDeviceNameOnlyWhenMultiple() const override;
    int touchTimeout() const override;
    int notificationExtraTime() const override;
    QString primaryAction() const override;

private Q_SLOTS:
    void onConfigFileChanged(const QString &path);

private:
    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;
    QFileSystemWatcher *m_fileWatcher;

    // Configuration keys now defined in shared/config/configuration_keys.h

    /**
     * @brief Template helper for reading config entries
     * Reduces boilerplate in configuration reading methods
     */
    template<typename T>
    T readConfigEntry(const char* key, const T& defaultValue) const {
        return m_configGroup.readEntry(key, defaultValue);
    }
};

} // namespace Runner
} // namespace YubiKeyOath
