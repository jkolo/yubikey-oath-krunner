/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemon_configuration.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

DaemonConfiguration::DaemonConfiguration(QObject *parent)
    : ConfigurationProvider(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("yubikey-oathrc")))
    , m_configGroup(m_config->group(QStringLiteral("General")))
{
}

void DaemonConfiguration::reload()
{
    m_config->reparseConfiguration();
    m_configGroup = m_config->group(QStringLiteral("General"));
    Q_EMIT configurationChanged();
}

bool DaemonConfiguration::showNotifications() const
{
    return readConfigEntry(ConfigKeys::SHOW_NOTIFICATIONS, true);
}

bool DaemonConfiguration::showUsername() const
{
    return readConfigEntry(ConfigKeys::SHOW_USERNAME, true);
}

bool DaemonConfiguration::showCode() const
{
    // NOTE: Default is 'true' in daemon (different from KRunnerConfiguration which defaults to 'false')
    // This is intentional - daemon shows code by default for notifications
    return readConfigEntry(ConfigKeys::SHOW_CODE, true);
}

bool DaemonConfiguration::showDeviceName() const
{
    // NOTE: Default is 'true' in daemon (different from KRunnerConfiguration which defaults to 'false')
    // This is intentional - daemon shows device name by default in notifications
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME, true);
}

bool DaemonConfiguration::showDeviceNameOnlyWhenMultiple() const
{
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME_ONLY_WHEN_MULTIPLE, true);
}

int DaemonConfiguration::touchTimeout() const
{
    // NOTE: Default is 15 seconds in daemon (different from KRunnerConfiguration which defaults to 10)
    // This is intentional - daemon uses longer timeout for background operations
    return readConfigEntry(ConfigKeys::TOUCH_TIMEOUT, 15);
}

int DaemonConfiguration::notificationExtraTime() const
{
    // Default is 15 seconds - matches Config UI default
    return readConfigEntry(ConfigKeys::NOTIFICATION_EXTRA_TIME, 15);
}

QString DaemonConfiguration::primaryAction() const
{
    return readConfigEntry(ConfigKeys::PRIMARY_ACTION, QStringLiteral("copy"));
}

} // namespace Daemon
} // namespace YubiKeyOath
