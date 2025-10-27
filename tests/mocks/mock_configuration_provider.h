/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "shared/config/configuration_provider.h"

namespace YubiKeyOath {
namespace Shared {

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
        , m_showUsername(true)
        , m_showCode(false)
        , m_showDeviceName(false)
        , m_showDeviceNameOnlyWhenMultiple(true)
        , m_touchTimeout(15)
        , m_notificationExtraTime(5)
        , m_primaryAction(QStringLiteral("copy"))
    {
    }

    // ConfigurationProvider interface implementation
    bool showNotifications() const override {
        return m_showNotifications;
    }

    bool showUsername() const override {
        return m_showUsername;
    }

    bool showCode() const override {
        return m_showCode;
    }

    bool showDeviceName() const override {
        return m_showDeviceName;
    }

    bool showDeviceNameOnlyWhenMultiple() const override {
        return m_showDeviceNameOnlyWhenMultiple;
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

    void setShowUsername(bool value) {
        m_showUsername = value;
        Q_EMIT configurationChanged();
    }

    void setShowCode(bool value) {
        m_showCode = value;
        Q_EMIT configurationChanged();
    }

    void setShowDeviceName(bool value) {
        m_showDeviceName = value;
        Q_EMIT configurationChanged();
    }

    void setShowDeviceNameOnlyWhenMultiple(bool value) {
        m_showDeviceNameOnlyWhenMultiple = value;
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
        m_showUsername = true;
        m_showCode = false;
        m_showDeviceName = false;
        m_showDeviceNameOnlyWhenMultiple = true;
        m_touchTimeout = 15;
        m_notificationExtraTime = 5;
        m_primaryAction = QStringLiteral("copy");
        Q_EMIT configurationChanged();
    }

private:
    bool m_showNotifications;
    bool m_showUsername;
    bool m_showCode;
    bool m_showDeviceName;
    bool m_showDeviceNameOnlyWhenMultiple;
    int m_touchTimeout;
    int m_notificationExtraTime;
    QString m_primaryAction;
};

} // namespace Shared
} // namespace YubiKeyOath
