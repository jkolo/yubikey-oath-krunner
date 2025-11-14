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
#include <QFileSystemWatcher>
#include <QObject>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Configuration reader for daemon
 *
 * Reads settings from yubikey-oathrc file for daemon operations
 *
 * @note Inherits from both QObject (for signals) and ConfigurationProvider (pure interface)
 */
class DaemonConfiguration : public QObject, public Shared::ConfigurationProvider
{
    Q_OBJECT

public:
    explicit DaemonConfiguration(QObject *parent = nullptr);

    /**
     * @brief Reloads configuration from file
     */
    void reload() override;

    bool showNotifications() const override;
    bool showUsername() const override;
    bool showCode() const override;
    bool showDeviceName() const override;
    bool showDeviceNameOnlyWhenMultiple() const override;
    int touchTimeout() const override;
    int notificationExtraTime() const override;
    QString primaryAction() const override;

    // Caching settings
    bool enableCredentialsCache() const override;
    int deviceReconnectTimeout() const override;
    int credentialSaveRateLimit() const override;

Q_SIGNALS:
    /**
     * @brief Emitted when configuration has been reloaded
     *
     * Components can connect to this signal to refresh their cached configuration values
     * or update active operations (e.g., adjust timer timeouts).
     */
    void configurationChanged();

private Q_SLOTS:
    void onConfigFileChanged(const QString &path);

private:
    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;
    QFileSystemWatcher *m_fileWatcher;

    // Configuration keys now defined in shared/config/configuration_keys.h

    template<typename T>
    T readConfigEntry(const char* key, const T& defaultValue) const {
        return m_configGroup.readEntry(key, defaultValue);
    }
};

} // namespace Daemon
} // namespace YubiKeyOath
