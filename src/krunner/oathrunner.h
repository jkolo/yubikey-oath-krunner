/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Qt includes
#include <QObject>
#include <QString>
#include <memory>

// KDE includes
#include <KRunner/AbstractRunner>
#include <KRunner/Action>
#include <KRunner/QueryMatch>

// Local includes - core components
// Forward declarations for D-Bus proxy classes
namespace YubiKeyOath {
namespace Shared {
class OathManagerProxy;
class OathDeviceProxy;
}
}

// Local includes - runner components
#include "config/krunner_configuration.h"
#include "actions/action_manager.h"
#include "matching/match_builder.h"

namespace YubiKeyOath {
namespace Runner {

// Forward declarations
class PasswordDialog;

/**
 * @brief KRunner plugin for generating YubiKey OATH TOTP codes
 *
 * Refactored to follow SOLID principles:
 * - Single Responsibility: Only handles KRunner framework integration
 * - Open/Closed: Easy to extend with new components
 * - Liskov Substitution: All components implement clear interfaces
 * - Interface Segregation: ConfigurationProvider interface
 * - Dependency Inversion: Depends on abstractions (ConfigurationProvider)
 */
class OathRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    explicit OathRunner(QObject *parent, const KPluginMetaData &metaData);
    ~OathRunner() override;

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

protected Q_SLOTS:
    void init() override;

private Q_SLOTS:
    void onDeviceConnected(Shared::OathDeviceProxy *device);
    void onDeviceDisconnected(const QString &deviceId);
    void onCredentialsUpdated();
    void onDaemonUnavailable();
    void onDevicePropertyChanged(Shared::OathDeviceProxy *device);

private:
    void setupActions();
    void reloadConfiguration() override;
    void updateDeviceStateCache();

    /**
     * @brief Shows password dialog for device authorization
     * @param deviceId Device ID requiring password
     * @param deviceName Friendly device name
     *
     * Creates non-modal password dialog using PasswordDialogHelper.
     * On success, shows notification. On failure, dialog stays open with error.
     */
    void showPasswordDialog(const QString &deviceId,
                           const QString &deviceName);

private:
    // Core components
    Shared::OathManagerProxy *m_manager; // Singleton - not owned

    // Runner components - thin client for match building
    std::unique_ptr<KRunnerConfiguration> m_config;
    std::unique_ptr<ActionManager> m_actionManager;
    std::unique_ptr<MatchBuilder> m_matchBuilder;

    // Actions
    KRunner::Actions m_actions;

    // Translated keywords for "Add OATH" matching
    QStringList m_addOathKeywords;

    // Device state cache (updated on device property changes)
    int m_cachedReadyDevices{0};
    int m_cachedInitializingDevices{0};
};

} // namespace Runner
} // namespace YubiKeyOath
