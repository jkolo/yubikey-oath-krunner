/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "krunner_configuration.h"
#include <QDebug>
#include <KConfigGroup>
#include <KConfig>

namespace KRunner {
namespace YubiKey {

KRunnerConfiguration::KRunnerConfiguration(std::function<KConfigGroup()> configGroup, QObject *parent)
    : ConfigurationProvider(parent)
    , m_configGroup(std::move(configGroup))
{
}

bool KRunnerConfiguration::showNotifications() const
{
    return readConfigEntry(CONFIG_SHOW_NOTIFICATIONS, true);
}

bool KRunnerConfiguration::showUsername() const
{
    return readConfigEntry(CONFIG_SHOW_USERNAME, true);
}

bool KRunnerConfiguration::showCode() const
{
    return readConfigEntry(CONFIG_SHOW_CODE, false);
}

bool KRunnerConfiguration::showDeviceName() const
{
    return readConfigEntry(CONFIG_SHOW_DEVICE_NAME, false);
}

bool KRunnerConfiguration::showDeviceNameOnlyWhenMultiple() const
{
    return readConfigEntry(CONFIG_SHOW_DEVICE_NAME_ONLY_WHEN_MULTIPLE, true);
}

int KRunnerConfiguration::touchTimeout() const
{
    return readConfigEntry(CONFIG_TOUCH_TIMEOUT, 10);
}

int KRunnerConfiguration::notificationExtraTime() const
{
    return readConfigEntry(CONFIG_NOTIFICATION_EXTRA_TIME, 15);
}

QString KRunnerConfiguration::primaryAction() const
{
    return readConfigEntry(CONFIG_PRIMARY_ACTION, QStringLiteral("copy"));
}

} // namespace YubiKey
} // namespace KRunner
