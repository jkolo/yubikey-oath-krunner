/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "krunner/config/configuration_provider.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Mock implementation of ConfigurationProvider for unit tests
 *
 * Provides controllable configuration values for testing components
 * that depend on ConfigurationProvider interface.
 *
 * Usage:
 * @code
 * MockConfigurationProvider config;
 * config.setShowNotifications(false);
 * config.setDisplayFormat("full");
 *
 * // Use in component under test
 * MyComponent component(&config);
 * @endcode
 */
class MockConfigurationProvider : public ConfigurationProvider
{
    Q_OBJECT

public:
    explicit MockConfigurationProvider(QObject *parent = nullptr)
        : ConfigurationProvider(parent)
        , m_showNotifications(true)
        , m_displayFormat(QStringLiteral("name_user"))
        , m_touchTimeout(15)
        , m_notificationExtraTime(5)
        , m_primaryAction(QStringLiteral("copy"))
    {
    }

    // ConfigurationProvider interface implementation
    bool showNotifications() const override {
        return m_showNotifications;
    }

    QString displayFormat() const override {
        return m_displayFormat;
    }

    int touchTimeout() const override {
        return m_touchTimeout;
    }

    int notificationExtraTime() const override {
        return m_notificationExtraTime;
    }

    QString primaryAction() const override {
        return m_primaryAction;
    }

    // Test control methods
    void setShowNotifications(bool value) {
        m_showNotifications = value;
        Q_EMIT configurationChanged();
    }

    void setDisplayFormat(const QString &value) {
        m_displayFormat = value;
        Q_EMIT configurationChanged();
    }

    void setTouchTimeout(int value) {
        m_touchTimeout = value;
        Q_EMIT configurationChanged();
    }

    void setNotificationExtraTime(int value) {
        m_notificationExtraTime = value;
        Q_EMIT configurationChanged();
    }

    void setPrimaryAction(const QString &value) {
        m_primaryAction = value;
        Q_EMIT configurationChanged();
    }

    // Helper: Reset to default values
    void reset() {
        m_showNotifications = true;
        m_displayFormat = QStringLiteral("name_user");
        m_touchTimeout = 15;
        m_notificationExtraTime = 5;
        m_primaryAction = QStringLiteral("copy");
        Q_EMIT configurationChanged();
    }

private:
    bool m_showNotifications;
    QString m_displayFormat;
    int m_touchTimeout;
    int m_notificationExtraTime;
    QString m_primaryAction;
};

} // namespace YubiKey
} // namespace KRunner
