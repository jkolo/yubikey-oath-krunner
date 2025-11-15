/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OATH_MANAGER_PROXY_H
#define OATH_MANAGER_PROXY_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QMap>
#include "oath_device_proxy.h"
#include "oath_device_session_proxy.h"
#include "types/device_state.h"

// Forward declarations
class QDBusInterface;
class QDBusServiceWatcher;
class QDBusMessage;
class QDBusPendingCallWatcher;

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
 * OathManagerProxy (singleton) ← YOU ARE HERE
 *     ↓ owns
 * OathDeviceProxy (per device)
 *     ↓ owns
 * OathCredentialProxy (per credential)
 * ```
 *
 * Usage:
 * ```cpp
 * auto *manager = OathManagerProxy::instance();
 * connect(manager, &OathManagerProxy::deviceConnected, this, &MyClass::onDeviceConnected);
 *
 * QList<OathDeviceProxy*> devices = manager->devices();
 * QList<OathCredentialProxy*> allCredentials = manager->getAllCredentials();
 * ```
 */
class OathManagerProxy : public QObject
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
    static OathManagerProxy* instance(QObject *parent = nullptr);

    ~OathManagerProxy() override;

    // ========== Manager Properties ==========

    QString version() const { return m_version; }
    int deviceCount() const { return static_cast<int>(m_devices.size()); }
    int totalCredentials() const;

    // ========== Device Management ==========

    /**
     * @brief Gets all device proxies
     * @return List of device proxy pointers (owned by this object)
     */
    QList<OathDeviceProxy*> devices() const;

    /**
     * @brief Gets specific device by ID
     * @param deviceId Device ID (hex string)
     * @return Device proxy pointer or nullptr if not found
     */
    OathDeviceProxy* getDevice(const QString &deviceId) const;

    /**
     * @brief Gets device session proxy by ID
     * @param deviceId Device ID (hex string)
     * @return Device session proxy pointer or nullptr if not found
     */
    OathDeviceSessionProxy* getDeviceSession(const QString &deviceId) const;

    /**
     * @brief Gets all credential proxies from all devices
     * @return List of credential proxy pointers
     *
     * Aggregates credentials from all connected devices.
     */
    QList<OathCredentialProxy*> getAllCredentials() const;

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
    void deviceConnected(OathDeviceProxy *device);

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

    /**
     * @brief Emitted when device properties change (name, connection status, password state)
     * @param device Device proxy with changed properties
     */
    void devicePropertyChanged(OathDeviceProxy *device);

private Q_SLOTS:
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusObjectPath &objectPath,
                            const QStringList &interfaces);
    void onManagerPropertiesChanged(const QString &interfaceName,
                                   const QVariantMap &changedProperties,
                                   const QStringList &invalidatedProperties);
    void onDBusServiceRegistered(const QString &serviceName);
    void onDBusServiceUnregistered(const QString &serviceName);
    void onGetManagedObjectsFinished(QDBusPendingCallWatcher *watcher);

private:  // NOLINT(readability-redundant-access-specifiers) - Required to close Q_SLOTS section for moc
    explicit OathManagerProxy(QObject *parent = nullptr);

    void setupServiceWatcher();
    void connectToSignals();
    void refreshManagedObjects();

    void addDeviceProxy(const QString &devicePath,
                       const QVariantMap &deviceProperties,
                       const QVariantMap &sessionProperties,
                       const QHash<QString, QVariantMap> &credentialObjects);
    void removeDeviceProxy(const QString &devicePath);

    // Singleton instance
    static OathManagerProxy *s_instance;

    QDBusInterface *m_managerInterface{nullptr};
    QDBusInterface *m_objectManagerInterface{nullptr};
    QDBusServiceWatcher *m_serviceWatcher{nullptr};
    bool m_daemonAvailable{false};

    // Manager properties
    QString m_version;

    // Device and session proxies (owned by this object via Qt parent-child)
    QHash<QString, OathDeviceProxy*> m_devices; // key: device ID
    QHash<QString, OathDeviceSessionProxy*> m_deviceSessions; // key: device ID

    static constexpr const char *SERVICE_NAME = "pl.jkolo.yubikey.oath.daemon";
    static constexpr const char *MANAGER_PATH = "/pl/jkolo/yubikey/oath";
    static constexpr const char *MANAGER_INTERFACE = "pl.jkolo.yubikey.oath.Manager";
    static constexpr const char *OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
    static constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
    static constexpr const char *DEVICE_INTERFACE = "pl.jkolo.yubikey.oath.Device";
    static constexpr const char *DEVICE_SESSION_INTERFACE = "pl.jkolo.yubikey.oath.DeviceSession";
    static constexpr const char *CREDENTIAL_INTERFACE = "pl.jkolo.yubikey.oath.Credential";
};

} // namespace Shared
} // namespace YubiKeyOath

#endif // OATH_MANAGER_PROXY_H
