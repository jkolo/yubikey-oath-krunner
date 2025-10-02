/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Interface for accessing plugin configuration with reactive updates
 *
 * Single Responsibility: Provide access to configuration settings and notify on changes
 * Interface Segregation: Clients depend only on configuration access, not implementation
 * Dependency Inversion: YubiKeyRunner depends on abstraction, not concrete KConfig
 * Observer Pattern: Emits configurationChanged() signal for reactive updates
 */
class ConfigurationProvider : public QObject
{
    Q_OBJECT

public:
    explicit ConfigurationProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ConfigurationProvider();

    /**
     * @brief Gets notification display preference
     * @return true if notifications should be shown
     */
    virtual bool showNotifications() const = 0;

    /**
     * @brief Gets display format template
     * @return Format string for credential display
     */
    virtual QString displayFormat() const = 0;

    /**
     * @brief Gets touch timeout setting
     * @return Timeout in seconds
     */
    virtual int touchTimeout() const = 0;

    /**
     * @brief Gets notification extra time setting
     * @return Additional notification time in seconds
     */
    virtual int notificationExtraTime() const = 0;

    /**
     * @brief Gets primary action preference
     * @return Primary action ID ("copy" or "type")
     */
    virtual QString primaryAction() const = 0;

Q_SIGNALS:
    /**
     * @brief Emitted when configuration has been reloaded
     *
     * Components can connect to this signal to refresh their cached configuration values
     * or update active operations (e.g., adjust timer timeouts).
     */
    void configurationChanged();
};

} // namespace YubiKey
} // namespace KRunner
