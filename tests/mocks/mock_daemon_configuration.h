/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "shared/config/configuration_provider.h"
#include <QObject>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of DaemonConfiguration for unit tests
 *
 * Extends MockConfigurationProvider with daemon-specific methods:
 * - enableCredentialsCache()
 * - credentialSaveRateLimit()
 *
 * Usage:
 * @code
 * MockDaemonConfiguration config;
 * config.setEnableCredentialsCache(true);
 * config.setShowNotifications(false);
 *
 * // Use in component under test
 * CredentialService service(&deviceManager, &database, &config, this);
 * @endcode
 */
class MockDaemonConfiguration : public QObject, public Shared::ConfigurationProvider
{
    Q_OBJECT

public:
    explicit MockDaemonConfiguration(QObject *parent = nullptr)
        : QObject(parent)
        , m_showNotifications(true)
        , m_showUsername(true)
        , m_showCode(false)
        , m_showDeviceName(false)
        , m_showDeviceNameOnlyWhenMultiple(true)
        , m_touchTimeout(15)
        , m_notificationExtraTime(5)
        , m_primaryAction(QStringLiteral("copy"))
        , m_deviceReconnectTimeout(30)
        , m_enableCredentialsCache(true)  // Default: cache enabled
        , m_credentialSaveRateLimit(1000)  // Default: 1 second
        , m_pcscRateLimitMs(0)  // Default: no delay
    {
    }

    // ConfigurationProvider interface implementation
    void reload() override {
        // Mock does nothing on reload (configuration is set via setters)
    }

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

    int deviceReconnectTimeout() const override {
        return m_deviceReconnectTimeout;
    }

    // DaemonConfiguration-specific methods
    bool enableCredentialsCache() const override {
        return m_enableCredentialsCache;
    }

    int credentialSaveRateLimit() const override {
        return m_credentialSaveRateLimit;
    }

    int pcscRateLimitMs() const override {
        return m_pcscRateLimitMs;
    }

Q_SIGNALS:
    void configurationChanged();

public:
    // Test control methods - ConfigurationProvider interface
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

    void setDeviceReconnectTimeout(int value) {
        m_deviceReconnectTimeout = value;
        Q_EMIT configurationChanged();
    }

    // Test control methods - DaemonConfiguration-specific
    void setEnableCredentialsCache(bool value) {
        m_enableCredentialsCache = value;
        Q_EMIT configurationChanged();
    }

    void setCredentialSaveRateLimit(int value) {
        m_credentialSaveRateLimit = value;
        Q_EMIT configurationChanged();
    }

    void setPcscRateLimitMs(int value) {
        m_pcscRateLimitMs = value;
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
        m_deviceReconnectTimeout = 30;
        m_enableCredentialsCache = true;
        m_credentialSaveRateLimit = 1000;
        m_pcscRateLimitMs = 0;
        Q_EMIT configurationChanged();
    }

private:
    // ConfigurationProvider interface fields
    bool m_showNotifications;
    bool m_showUsername;
    bool m_showCode;
    bool m_showDeviceName;
    bool m_showDeviceNameOnlyWhenMultiple;
    int m_touchTimeout;
    int m_notificationExtraTime;
    QString m_primaryAction;
    int m_deviceReconnectTimeout;

    // DaemonConfiguration-specific fields
    bool m_enableCredentialsCache;
    int m_credentialSaveRateLimit;
    int m_pcscRateLimitMs;
};

} // namespace Daemon
} // namespace YubiKeyOath
