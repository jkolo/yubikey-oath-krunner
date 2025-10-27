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
class YubiKeyManagerProxy;
class YubiKeyDeviceProxy;
}
}

// Local includes - runner components
#include "config/configuration_provider.h"
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
class YubiKeyRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    explicit YubiKeyRunner(QObject *parent, const KPluginMetaData &metaData);
    ~YubiKeyRunner() override;

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

protected Q_SLOTS:
    void init() override;

private Q_SLOTS:
    void onDeviceConnected(Shared::YubiKeyDeviceProxy *device);
    void onDeviceDisconnected(const QString &deviceId);
    void onCredentialsUpdated();
    void onDaemonUnavailable();

private:
    void setupActions();
    void reloadConfiguration() override;

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
    Shared::YubiKeyManagerProxy *m_manager; // Singleton - not owned

    // Runner components - thin client for match building
    std::unique_ptr<ConfigurationProvider> m_config;
    std::unique_ptr<ActionManager> m_actionManager;
    std::unique_ptr<MatchBuilder> m_matchBuilder;

    // Actions
    KRunner::Actions m_actions;
};

} // namespace Runner
} // namespace YubiKeyOath
