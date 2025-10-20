/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDBusConnection>
#include <QObject>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Helper utilities for D-Bus connection management
 *
 * Provides convenient wrappers for common D-Bus operations to reduce
 * boilerplate code. Header-only for zero overhead.
 *
 * @par Use Cases
 * - Connect D-Bus signals with less boilerplate
 * - Simplify repeated signal connection patterns
 * - Type-safe D-Bus operations
 */
namespace DBusConnectionHelper {

/**
 * @brief Connect D-Bus signal to receiver's signal
 *
 * Simplifies connecting D-Bus signals by reducing boilerplate.
 * Uses Qt's signal-to-signal connection for automatic forwarding.
 *
 * @param service D-Bus service name (e.g., "org.kde.example")
 * @param path D-Bus object path (e.g., "/Device")
 * @param interface D-Bus interface name (e.g., "org.kde.example.Device")
 * @param signalName Name of the D-Bus signal to connect
 * @param receiver Object that will receive/forward the signal
 * @param receiverSignal Signal on receiver object (use SIGNAL() macro)
 *
 * @return true if connection successful, false otherwise
 *
 * @par Example
 * @code
 * // Before:
 * QDBusConnection::sessionBus().connect(
 *     SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME,
 *     "DeviceConnected", this, SIGNAL(deviceConnected(QString))
 * );
 *
 * // After:
 * DBusConnectionHelper::connectSignal(
 *     SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME,
 *     "DeviceConnected", this, SIGNAL(deviceConnected(QString))
 * );
 * @endcode
 */
inline bool connectSignal(
    const QString& service,
    const QString& path,
    const QString& interface,
    const QString& signalName,
    QObject* receiver,
    const char* receiverSignal)
{
    return QDBusConnection::sessionBus().connect(
        service,
        path,
        interface,
        signalName,
        receiver,
        receiverSignal
    );
}

/**
 * @brief Connect multiple D-Bus signals at once
 *
 * Connects multiple signals from the same service/path/interface
 * to corresponding receiver signals. Reduces repetitive code when
 * setting up multiple signal connections.
 *
 * @param service D-Bus service name
 * @param path D-Bus object path
 * @param interface D-Bus interface name
 * @param receiver Object that will receive/forward signals
 * @param signalMappings List of {dbusSignal, receiverSignal} pairs
 *
 * @return Number of successful connections
 *
 * @par Example
 * @code
 * int connected = DBusConnectionHelper::connectSignals(
 *     SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME, this, {
 *         {"DeviceConnected", SIGNAL(deviceConnected(QString))},
 *         {"DeviceDisconnected", SIGNAL(deviceDisconnected(QString))},
 *         {"CredentialsUpdated", SIGNAL(credentialsUpdated(QString))}
 *     }
 * );
 * @endcode
 */
inline int connectSignals(
    const QString& service,
    const QString& path,
    const QString& interface,
    QObject* receiver,
    const std::initializer_list<std::pair<const char*, const char*>>& signalMappings)
{
    int successCount = 0;
    for (const auto& [dbusSignal, receiverSignal] : signalMappings) {
        if (connectSignal(service, path, interface,
                         QString::fromLatin1(dbusSignal),
                         receiver, receiverSignal)) {
            ++successCount;
        }
    }
    return successCount;
}

} // namespace DBusConnectionHelper

} // namespace YubiKey
} // namespace KRunner
