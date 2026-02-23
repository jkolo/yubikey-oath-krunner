/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <functional>
#include "../../shared/common/result.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;  // For Result<T>

/**
 * @brief Coordinates device reconnection with exponential backoff
 *
 * Extracted from OathDeviceManager to handle the complexity of device
 * reconnection after card reset (SCARD_W_RESET_CARD).
 *
 * Reconnection strategy:
 * - Initial delay: 10ms (let external app release card)
 * - Calls reconnect function once (device has built-in exponential backoff)
 * - Emits success/failure signals
 *
 * Usage:
 * @code
 * DeviceReconnectCoordinator coordinator;
 * connect(&coordinator, &DeviceReconnectCoordinator::reconnectStarted, ...);
 * connect(&coordinator, &DeviceReconnectCoordinator::reconnectCompleted, ...);
 *
 * coordinator.setReconnectFunction([&](const QString &readerName) {
 *     return device->reconnectCardHandle(readerName);
 * });
 * coordinator.startReconnect(deviceId, readerName, command);
 * @endcode
 */
class DeviceReconnectCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Function type for reconnecting device
     * @param readerName PC/SC reader name
     * @return Result indicating success or error
     */
    using ReconnectFunction = std::function<Result<void>(const QString &readerName)>;

    /**
     * @brief Constructs coordinator
     * @param parent Parent QObject
     */
    explicit DeviceReconnectCoordinator(QObject *parent = nullptr);

    /**
     * @brief Destructor - stops any pending reconnect
     */
    ~DeviceReconnectCoordinator() override;

    /**
     * @brief Sets the function to call for reconnection
     * @param func Function that performs actual device reconnection
     *
     * Must be set before calling startReconnect().
     */
    void setReconnectFunction(ReconnectFunction func);

    /**
     * @brief Starts asynchronous reconnection process
     * @param deviceId Device ID being reconnected
     * @param readerName PC/SC reader name
     * @param command Command to retry (for logging, not used internally)
     *
     * Emits reconnectStarted() immediately, then schedules reconnection.
     * Emits reconnectCompleted() when done.
     */
    void startReconnect(const QString &deviceId,
                        const QString &readerName,
                        const QByteArray &command);

    /**
     * @brief Cancels any pending reconnection
     *
     * If reconnection is in progress, it will be cancelled.
     * No signals will be emitted after cancellation.
     */
    void cancel();

    /**
     * @brief Checks if reconnection is in progress
     * @return true if reconnecting
     */
    [[nodiscard]] bool isReconnecting() const;

    /**
     * @brief Gets current device ID being reconnected
     * @return Device ID or empty string if not reconnecting
     */
    [[nodiscard]] QString currentDeviceId() const;

Q_SIGNALS:
    /**
     * @brief Emitted when reconnection process starts
     * @param deviceId Device ID being reconnected
     */
    void reconnectStarted(const QString &deviceId);

    /**
     * @brief Emitted when reconnection completes
     * @param deviceId Device ID that was reconnected
     * @param success true if reconnection succeeded
     */
    void reconnectCompleted(const QString &deviceId, bool success);

private Q_SLOTS:
    /**
     * @brief Handles timer timeout - performs reconnection attempt
     */
    void onTimeout();

private:
    /**
     * @brief Cleans up reconnection state
     */
    void cleanup();

    ReconnectFunction m_reconnectFunc;
    QTimer *m_timer = nullptr;
    QString m_deviceId;
    QString m_readerName;
    QByteArray m_command;  ///< Stored for logging only

    static constexpr int INITIAL_DELAY_MS = 10;  ///< Initial delay before reconnect
};

} // namespace Daemon
} // namespace YubiKeyOath
