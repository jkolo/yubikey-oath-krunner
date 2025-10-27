/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "daemon_configuration.h"
#include <QStandardPaths>
#include <QFile>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

DaemonConfiguration::DaemonConfiguration(QObject *parent)
    : ConfigurationProvider(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("yubikey-oathrc")))
    , m_configGroup(m_config->group(QStringLiteral("General")))
    , m_fileWatcher(new QFileSystemWatcher(this))
{
    // Get config file path
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/yubikey-oathrc");

    qDebug() << "DaemonConfiguration: Watching config file:" << configPath;

    // Watch config file for changes
    if (QFile::exists(configPath)) {
        m_fileWatcher->addPath(configPath);
    }

    // Connect file change signal
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &DaemonConfiguration::onConfigFileChanged);
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
    return readConfigEntry(ConfigKeys::SHOW_CODE, false);
}

bool DaemonConfiguration::showDeviceName() const
{
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME, false);
}

bool DaemonConfiguration::showDeviceNameOnlyWhenMultiple() const
{
    return readConfigEntry(ConfigKeys::SHOW_DEVICE_NAME_ONLY_WHEN_MULTIPLE, true);
}

int DaemonConfiguration::touchTimeout() const
{
    return readConfigEntry(ConfigKeys::TOUCH_TIMEOUT, 10);
}

int DaemonConfiguration::notificationExtraTime() const
{
    return readConfigEntry(ConfigKeys::NOTIFICATION_EXTRA_TIME, 15);
}

QString DaemonConfiguration::primaryAction() const
{
    return readConfigEntry(ConfigKeys::PRIMARY_ACTION, QStringLiteral("copy"));
}

void DaemonConfiguration::onConfigFileChanged(const QString &path)
{
    qDebug() << "DaemonConfiguration: Config file changed:" << path;

    // Reload configuration from file
    reload();

    // Re-add file to watch list (QFileSystemWatcher removes it after change on some systems)
    if (!m_fileWatcher->files().contains(path)) {
        m_fileWatcher->addPath(path);
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
