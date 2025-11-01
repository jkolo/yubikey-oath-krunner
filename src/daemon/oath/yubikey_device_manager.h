/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Qt includes
#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>

// STL includes
#include <memory>
#include <unordered_map>

// Local includes
#include "types/oath_credential.h"
#include "yubikey_oath_device.h"
#include "../pcsc/card_reader_monitor.h"
#include "common/result.h"

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manages multiple YubiKey devices for OATH (TOTP/HOTP) operations
 *
 * This class acts as a manager for multiple YubiKey devices, coordinating
 * PC/SC context, device lifecycle (hot-plug detection), and providing
 * access to individual device instances.
 *
 * Supports multiple YubiKey devices simultaneously via per-device instances.
 * Each device is represented by a YubiKeyOathDevice instance that manages
 * its own PC/SC connection, credentials, and authentication state.
 *
 * Responsibilities:
 * - PC/SC context management (shared by all devices)
 * - Device hot-plug detection via CardReaderMonitor
 * - Device connection/disconnection lifecycle
 * - Credential aggregation from multiple devices via getCredentials()
 * - Device access via getDevice(deviceId)
 * - Signal forwarding from individual devices for multi-device monitoring
 *
 * Usage Pattern:
 * - For device-specific operations: use getDevice(deviceId) and call methods on the device
 * - For multi-device aggregation: use getCredentials() to get all credentials
 * - For device lifecycle: listen to deviceConnected/deviceDisconnected signals
 */
class YubiKeyDeviceManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKeyDeviceManager instance
     * @param parent Parent QObject
     */
    explicit YubiKeyDeviceManager(QObject *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~YubiKeyDeviceManager();

    // Device lifecycle management
    /**
     * @brief Initializes connection to YubiKey OATH application
     * @return Result indicating success or containing error message
     */
    Result<void> initialize();

    /**
     * @brief Cleans up resources and disconnects
     */
    void cleanup();

    /**
     * @brief Checks if any devices are currently connected
     * @return true if at least one device is connected
     */
    bool hasConnectedDevices() const;

    // Credential operations
    /**
     * @brief Gets list of available OATH credentials from all connected devices
     * @return Aggregated list of credentials from all devices
     *
     * This is an aggregation method that collects credentials from all
     * connected devices. For device-specific operations, use getDevice(deviceId).
     */
    QList<OathCredential> getCredentials();



    // Device query operations
    /**
     * @brief Gets list of all connected device IDs
     * @return List of device IDs for all currently connected YubiKeys
     */
    QStringList getConnectedDeviceIds() const;

    /**
     * @brief Gets YubiKeyOathDevice instance for specific device
     * @param deviceId Device ID to get
     * @return Pointer to device instance or nullptr if not found/connected
     *
     * Use this method to access device-specific operations.
     */
    YubiKeyOathDevice* getDevice(const QString &deviceId);

    /**
     * @brief Gets device by ID or first available device if ID is empty
     * @param deviceId Device ID to get (empty = use first connected device)
     * @return Pointer to device instance or nullptr if no devices available
     *
     * Convenience method that implements the common pattern:
     * - If deviceId is not empty: returns getDevice(deviceId)
     * - If deviceId is empty: returns first connected device
     * - If no devices connected: returns nullptr
     *
     * This eliminates the repetitive pattern found throughout the codebase.
     */
    YubiKeyOathDevice* getDeviceOrFirst(const QString &deviceId);

    /**
     * @brief Removes device from memory (called when device is forgotten)
     * @param deviceId Device ID to remove from memory
     *
     * This method clears the device from m_devices map, effectively
     * forgetting it from the daemon's runtime state. Used when a device
     * is removed from configuration/database via ForgetDevice().
     */
    void removeDeviceFromMemory(const QString &deviceId);

    /**
     * @brief Asynchronously reconnects to YubiKey after card reset
     * @param deviceId Device ID to reconnect
     * @param readerName PC/SC reader name to reconnect to
     * @param command APDU command to retry after successful reconnect
     *
     * This method handles card reset (SCARD_W_RESET_CARD) by performing:
     * 1. Full disconnect of device (frees card handle)
     * 2. Exponential backoff retry: 100ms, 200ms, 400ms, 800ms, 1600ms, 3000ms
     * 3. Reconnect attempt on each timer tick
     * 4. Emits reconnectCompleted(deviceId, success) when done
     *
     * The exponential backoff allows external apps (like ykman) to release
     * the card before we retry connection.
     */
    void reconnectDeviceAsync(const QString &deviceId, const QString &readerName, const QByteArray &command);

Q_SIGNALS:
    /**
     * @brief Emitted when YubiKey touch is required
     */
    void touchRequired();

    /**
     * @brief Emitted when an error occurs
     * @param error Error description
     */
    void errorOccurred(const QString &error);

    /**
     * @brief Emitted when credential list changes
     */
    void credentialsChanged();

    /**
     * @brief Emitted when a YubiKey device is connected
     * @param deviceId Device ID of the connected YubiKey
     */
    void deviceConnected(const QString &deviceId);

    /**
     * @brief Emitted when a YubiKey device is disconnected (physically removed)
     * @param deviceId Device ID of the disconnected YubiKey
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when a YubiKey device is forgotten (removed from config)
     * @param deviceId Device ID of the forgotten YubiKey
     */
    void deviceForgotten(const QString &deviceId);

    /**
     * @brief Emitted when asynchronous credential cache fetching completes for specific device
     * @param deviceId Device ID that was updated
     * @param credentials List of fetched credentials for this device
     */
    void credentialCacheFetchedForDevice(const QString &deviceId, const QList<OathCredential> &credentials);

    /**
     * @brief Emitted when device reconnect starts
     * @param deviceId Device ID that is being reconnected
     *
     * This signal is emitted when reconnectDeviceAsync() begins reconnect attempt.
     * Used by YubiKeyService to show reconnect notification.
     */
    void reconnectStarted(const QString &deviceId);

    /**
     * @brief Emitted when device reconnect completes (success or failure)
     * @param deviceId Device ID that was reconnected
     * @param success true if reconnect succeeded, false otherwise
     *
     * This signal is emitted after reconnectDeviceAsync() completes.
     * Used to notify YubiKeyOathDevice::onReconnectResult() which forwards
     * to OathSession to unblock waiting sendApdu().
     */
    void reconnectCompleted(const QString &deviceId, bool success);

private Q_SLOTS:
    /**
     * @brief Handles reader list change (device added/removed)
     */
    void onReaderListChanged();

    /**
     * @brief Handles card insertion event from monitor
     * @param readerName Name of reader with inserted card
     */
    void onCardInserted(const QString &readerName);

    /**
     * @brief Handles card removal event from monitor
     * @param readerName Name of reader with removed card
     */
    void onCardRemoved(const QString &readerName);

    /**
     * @brief Handles completion of asynchronous credential cache fetching for specific device
     * @param deviceId Device ID that was updated
     * @param credentials List of fetched credentials
     */
    void onCredentialCacheFetchedForDevice(const QString &deviceId, const QList<OathCredential> &credentials);

    /**
     * @brief Handles reconnect timer timeout (exponential backoff retry)
     *
     * Called by m_reconnectTimer on each timeout.
     * Attempts to reconnect to device, increases delay, or emits failure after max attempts.
     */
    void onReconnectTimer();

private:
    // Core PC/SC operations
    /**
     * @brief Connects to specific YubiKey device by reader name
     * @param readerName PC/SC reader name to connect to
     * @return Device ID (hex string) on success, empty string on failure
     *
     * Creates temporary OathSession to execute SELECT and get device ID.
     */
    QString connectToDevice(const QString &readerName);

    /**
     * @brief Disconnects from specific YubiKey device
     * @param deviceId Device ID to disconnect
     */
    void disconnectDevice(const QString &deviceId);

    // Member variables
    CardReaderMonitor *m_readerMonitor;
    mutable QMutex m_devicesMutex;  ///< Protects m_devices map from concurrent access

    // Use std::unordered_map for unique_ptr support (Qt containers don't support move-only types)
    struct QStringHash {
        std::size_t operator()(const QString &key) const noexcept {
            return qHash(key);
        }
    };
    std::unordered_map<QString, std::unique_ptr<YubiKeyOathDevice>, QStringHash> m_devices;  ///< deviceId → device instance mapping for multi-device support

    SCARDCONTEXT m_context = 0;  ///< PC/SC context (shared by all devices)
    bool m_initialized = false;  ///< Tracks initialization state

    // Reconnect state (for exponential backoff)
    QTimer *m_reconnectTimer = nullptr;  ///< Timer for exponential backoff reconnect
    QString m_reconnectDeviceId;         ///< Device ID being reconnected
    QString m_reconnectReaderName;       ///< Reader name for reconnection
    QByteArray m_reconnectCommand;       ///< Command to retry after reconnect
    int m_reconnectAttempt = 0;          ///< Current reconnect attempt number (0-based)
    static constexpr int MAX_RECONNECT_ATTEMPTS = 6;  ///< Maximum reconnect attempts
};
} // namespace Daemon
} // namespace YubiKeyOath
