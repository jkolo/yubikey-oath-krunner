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
    return m_configGroup().readEntry(CONFIG_SHOW_NOTIFICATIONS, true);
}

QString KRunnerConfiguration::displayFormat() const
{
    return m_configGroup().readEntry(CONFIG_DISPLAY_FORMAT, QStringLiteral("name_user"));
}

int KRunnerConfiguration::touchTimeout() const
{
    return m_configGroup().readEntry(CONFIG_TOUCH_TIMEOUT, 10);
}

int KRunnerConfiguration::notificationExtraTime() const
{
    return m_configGroup().readEntry(CONFIG_NOTIFICATION_EXTRA_TIME, 15);
}

QString KRunnerConfiguration::primaryAction() const
{
    return m_configGroup().readEntry(CONFIG_PRIMARY_ACTION, QStringLiteral("copy"));
}

} // namespace YubiKey
} // namespace KRunner
