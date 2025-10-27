/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "krunner_configuration.h"
#include <QDebug>
#include <KConfigGroup>
#include <KConfig>

namespace YubiKeyOath {
namespace Runner {
using namespace YubiKeyOath::Shared;

KRunnerConfiguration::KRunnerConfiguration(std::function<KConfigGroup()> configGroup, QObject *parent)
    : ConfigurationProvider(parent)
    , m_configGroup(std::move(configGroup))
{
}

bool KRunnerConfiguration::showNotifications() const
{
    return readConfigEntry(ConfigKeys::SHOW_NOTIFICATIONS, true);
}

bool KRunnerConfiguration::showUsername() const
{
    return readConfigEntry(ConfigKeys::SHOW_USERNAME, true);
}

bool KRunnerConfiguration::showCode() const
{
    // NOTE: Default is 'false' in krunner (different from DaemonConfiguration which defaults to 'true')
    // This is intentional - krunner shows code in match list, not in notification
    return readConfigEntry(ConfigKeys::SHOW_CODE, false);
}

bool KRunnerConfiguration::showDeviceName() const
{
    // NOTE: Default is 'false' in krunner (different from DaemonConfiguration which defaults to 'true')
    // This is intentional - krunner shows device in match list, not in notification
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME, false);
}

bool KRunnerConfiguration::showDeviceNameOnlyWhenMultiple() const
{
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME_ONLY_WHEN_MULTIPLE, true);
}

int KRunnerConfiguration::touchTimeout() const
{
    // NOTE: Default is 10 seconds in krunner (different from DaemonConfiguration which defaults to 15)
    // This is intentional - krunner uses shorter timeout for interactive operations
    return readConfigEntry(ConfigKeys::TOUCH_TIMEOUT, 10);
}

int KRunnerConfiguration::notificationExtraTime() const
{
    // NOTE: Default is 15 in krunner (different from DaemonConfiguration which defaults to 0)
    // This is intentional - krunner adds extra time for user visibility in interactive context
    return readConfigEntry(ConfigKeys::NOTIFICATION_EXTRA_TIME, 15);
}

QString KRunnerConfiguration::primaryAction() const
{
    return readConfigEntry(ConfigKeys::PRIMARY_ACTION, QStringLiteral("copy"));
}

} // namespace Runner
} // namespace YubiKeyOath
