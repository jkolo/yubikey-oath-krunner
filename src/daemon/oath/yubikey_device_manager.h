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

// STL includes
#include <memory>
#include <unordered_map>

// Local includes
#include "../../shared/types/oath_credential.h"
#include "yubikey_oath_device.h"
#include "../pcsc/card_reader_monitor.h"
#include "../../shared/common/result.h"

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace KRunner {
namespace YubiKey {

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
     * @brief Removes device from memory (called when device is forgotten)
     * @param deviceId Device ID to remove from memory
     *
     * This method clears the device from m_devices map, effectively
     * forgetting it from the daemon's runtime state. Used when a device
     * is removed from configuration/database via ForgetDevice().
     */
    void removeDeviceFromMemory(const QString &deviceId);

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
     * @brief Emitted when a YubiKey device is disconnected
     * @param deviceId Device ID of the disconnected YubiKey
     */
    void deviceDisconnected(const QString &deviceId);

    /**
     * @brief Emitted when asynchronous credential cache fetching completes for specific device
     * @param deviceId Device ID that was updated
     * @param credentials List of fetched credentials for this device
     */
    void credentialCacheFetchedForDevice(const QString &deviceId, const QList<OathCredential> &credentials);

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
};
} // namespace YubiKey
} // namespace KRunner
