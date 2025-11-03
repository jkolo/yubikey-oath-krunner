/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "krunner_configuration.h"
#include <QDebug>
#include <KConfigGroup>
#include <KConfig>
#include <KSharedConfig>
#include <QStandardPaths>
#include <QFile>

namespace YubiKeyOath {
namespace Runner {
using namespace YubiKeyOath::Shared;

KRunnerConfiguration::KRunnerConfiguration(QObject *parent)
    : ConfigurationProvider(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("yubikey-oathrc")))
    , m_configGroup(m_config->group(QStringLiteral("General")))
    , m_fileWatcher(new QFileSystemWatcher(this))
{
    // Get config file path
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/yubikey-oathrc");

    qDebug() << "KRunnerConfiguration: Watching config file:" << configPath;

    // Watch config file for changes
    if (QFile::exists(configPath)) {
        m_fileWatcher->addPath(configPath);
    }

    // Connect file change signal
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &KRunnerConfiguration::onConfigFileChanged);
}

void KRunnerConfiguration::reload()
{
    m_config->reparseConfiguration();
    m_configGroup = m_config->group(QStringLiteral("General"));
    Q_EMIT configurationChanged();
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

int KRunnerConfiguration::deviceReconnectTimeout() const
{
    return readConfigEntry(ConfigKeys::DEVICE_RECONNECT_TIMEOUT, 30);
}

void KRunnerConfiguration::onConfigFileChanged(const QString &path)
{
    qDebug() << "KRunnerConfiguration: Config file changed:" << path;

    // Reload configuration from file
    reload();

    // Re-add file to watch list (QFileSystemWatcher removes it after change on some systems)
    if (!m_fileWatcher->files().contains(path)) {
        m_fileWatcher->addPath(path);
    }
}

} // namespace Runner
} // namespace YubiKeyOath
