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
#include "../shared/dbus/yubikey_dbus_client.h"
#include "notification/dbus_notification_manager.h"
#include "clipboard/clipboard_manager.h"
#include "input/text_input_provider.h"
#include "workflows/touch_handler.h"

// Local includes - runner components
#include "config/configuration_provider.h"
#include "config/krunner_configuration.h"
#include "actions/action_executor.h"
#include "actions/action_manager.h"
#include "matching/match_builder.h"
#include "workflows/notification_orchestrator.h"
#include "workflows/touch_workflow_coordinator.h"

namespace KRunner {
namespace YubiKey {

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
    void onDeviceConnected(const QString &deviceId);
    void onDeviceDisconnected(const QString &deviceId);
    void onCredentialsUpdated(const QString &deviceId);
    void onDaemonUnavailable();
    void onNotificationRequested(const QString &title, const QString &message, int type);

private:
    void setupActions();
    void reloadConfiguration() override;
    void processCredentialAsync(const QString &credentialName,
                               const QString &displayName,
                               const QString &code,
                               bool requiresTouch,
                               const QString &actionId,
                               const QString &deviceId);

    void showPasswordDialogForDevice(const QString &deviceId, const QString &errorMessage);

private:
    // Core components
    std::unique_ptr<YubiKeyDBusClient> m_dbusClient;
    std::unique_ptr<DBusNotificationManager> m_notificationManager;
    std::unique_ptr<ClipboardManager> m_clipboardManager;
    std::unique_ptr<TextInputProvider> m_textInput;
    std::unique_ptr<TouchHandler> m_touchHandler;

    // Runner components (SOLID refactoring)
    std::unique_ptr<ConfigurationProvider> m_config;
    std::unique_ptr<ActionExecutor> m_actionExecutor;
    std::unique_ptr<ActionManager> m_actionManager;
    std::unique_ptr<MatchBuilder> m_matchBuilder;
    std::unique_ptr<NotificationOrchestrator> m_notificationOrchestrator;
    std::unique_ptr<TouchWorkflowCoordinator> m_touchWorkflowCoordinator;

    // Actions
    KRunner::Actions m_actions;

    // Configuration keys (for password migration)
    static constexpr const char *CONFIG_PASSWORD = "Password";
};

} // namespace YubiKey
} // namespace KRunner
