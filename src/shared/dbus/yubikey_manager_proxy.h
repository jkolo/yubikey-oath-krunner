/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include "yubikey_device_proxy.h"

// Forward declarations
class QDBusInterface;
class QDBusServiceWatcher;

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Manager proxy for YubiKey OATH daemon (singleton)
 *
 * This class represents the D-Bus manager object at path:
 * /pl/jkolo/yubikey/oath
 *
 * Interfaces:
 * - pl.jkolo.yubikey.oath.Manager (daemon properties)
 * - org.freedesktop.DBus.ObjectManager (hierarchical object discovery)
 *
 * Single Responsibility: Singleton proxy for manager D-Bus object
 * - Implements ObjectManager pattern: GetManagedObjects()
 * - Creates and manages device proxy objects (children)
 * - Monitors daemon availability
 * - Provides high-level API for all devices and credentials
 * - Emits signals: deviceConnected, deviceDisconnected, credentialsChanged
 *
 * Architecture:
 * ```
 * YubiKeyManagerProxy (singleton) ← YOU ARE HERE
 *     ↓ owns
 * YubiKeyDeviceProxy (per device)
 *     ↓ owns
 * YubiKeyCredentialProxy (per credential)
 * ```
 *
 * Usage:
 * ```cpp
 * auto *manager = YubiKeyManagerProxy::instance();
 * connect(manager, &YubiKeyManagerProxy::deviceConnected, this, &MyClass::onDeviceConnected);
 *
 * QList<YubiKeyDeviceProxy*> devices = manager->devices();
 * QList<YubiKeyCredentialProxy*> allCredentials = manager->getAllCredentials();
 * ```
 */
class YubiKeyManagerProxy : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Gets singleton instance
     * @param parent Parent object (only used on first call)
     * @return Singleton instance
     *
     * Creates instance on first call and monitors daemon availability.
     */
    static YubiKeyManagerProxy* instance(QObject *parent = nullptr);

    ~YubiKeyManagerProxy() override;

    // ========== Manager Properties ==========

    QString version() const { return m_version; }
    int deviceCount() const { return m_devices.size(); }
    int totalCredentials() const;

    // ========== Device Management ==========

    /**
     * @brief Gets all device proxies
     * @return List of device proxy pointers (owned by this object)
     */
    QList<YubiKeyDeviceProxy*> devices() const;

    /**
     * @brief Gets specific device by ID
     * @param deviceId Device ID (hex string)
     * @return Device proxy pointer or nullptr if not found
     */
    YubiKeyDeviceProxy* getDevice(const QString &deviceId) const;

    /**
     * @brief Gets all credential proxies from all devices
     * @return List of credential proxy pointers
     *
     * Aggregates credentials from all connected devices.
     */
    QList<YubiKeyCredentialProxy*> getAllCredentials() const;

    /**
     * @brief Checks if daemon is currently available
     * @return true if daemon is registered on D-Bus
     */
    bool isDaemonAvailable() const { return m_daemonAvailable; }

    /**
     * @brief Refreshes object tree from daemon
     *
     * Calls GetManagedObjects() to refresh all devices and credentials.
     * Emits appropriate signals for changes.
     * Call this after daemon reconnects.
     */
    void refresh();

Q_SIGNALS:
    /**
     * @brief Emitted when a YubiKey device is connected or discovered
     * @param device Device proxy (owned by ManagerProxy)
     */
    void deviceConnected(YubiKeyDeviceProxy *device);

    /**
     * @brief Emitted when a YubiKey device is disconnected
     * @param deviceId Device ID of disconnected YubiKey
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when credentials change (added/removed across any device)
     */
    void credentialsChanged();

    /**
     * @brief Emitted when daemon becomes available
     */
    void daemonAvailable();

    /**
     * @brief Emitted when daemon becomes unavailable
     */
    void daemonUnavailable();

private Q_SLOTS:
    void onInterfacesAdded(const QDBusObjectPath &objectPath,
                          const QVariantMap &interfacesAndProperties);
    void onInterfacesRemoved(const QDBusObjectPath &objectPath,
                            const QStringList &interfaces);
    void onManagerPropertiesChanged(const QString &interfaceName,
                                   const QVariantMap &changedProperties,
                                   const QStringList &invalidatedProperties);
    void onDBusServiceRegistered(const QString &serviceName);
    void onDBusServiceUnregistered(const QString &serviceName);

private:
    explicit YubiKeyManagerProxy(QObject *parent = nullptr);

    void setupServiceWatcher();
    void connectToSignals();
    void refreshManagedObjects();

    void addDeviceProxy(const QString &devicePath,
                       const QVariantMap &deviceProperties,
                       const QHash<QString, QVariantMap> &credentialObjects);
    void removeDeviceProxy(const QString &devicePath);

    // Singleton instance
    static YubiKeyManagerProxy *s_instance;

    QDBusInterface *m_managerInterface;
    QDBusInterface *m_objectManagerInterface;
    QDBusServiceWatcher *m_serviceWatcher;
    bool m_daemonAvailable;

    // Manager properties
    QString m_version;

    // Device proxies (owned by this object via Qt parent-child)
    QHash<QString, YubiKeyDeviceProxy*> m_devices; // key: device ID

    static constexpr const char *SERVICE_NAME = "pl.jkolo.yubikey.oath.daemon";
    static constexpr const char *MANAGER_PATH = "/pl/jkolo/yubikey/oath";
    static constexpr const char *MANAGER_INTERFACE = "pl.jkolo.yubikey.oath.Manager";
    static constexpr const char *OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
    static constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
    static constexpr const char *DEVICE_INTERFACE = "pl.jkolo.yubikey.oath.Device";
    static constexpr const char *CREDENTIAL_INTERFACE = "pl.jkolo.yubikey.oath.Credential";
};

} // namespace Shared
} // namespace YubiKeyOath
