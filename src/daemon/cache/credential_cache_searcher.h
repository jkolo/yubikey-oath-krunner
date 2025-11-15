/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <optional>

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathDeviceManager;
class OathDatabase;
class DaemonConfiguration;

/**
 * @brief Searches for cached credentials in offline devices
 *
 * Single Responsibility: Search database cache for credentials when devices are offline.
 *
 * This class encapsulates the logic for finding credentials in the database cache
 * when the corresponding YubiKey device is not currently connected. It considers:
 * - Configuration (whether cache is enabled)
 * - Device connection status (only searches offline devices)
 * - Optional device hints (searches specific device first)
 *
 * Extracted from OathActionCoordinator to follow Single Responsibility Principle.
 */
class CredentialCacheSearcher
{
public:
    /**
     * @brief Constructs credential cache searcher
     * @param deviceManager Device manager to check connection status
     * @param database Database to search for cached credentials
     * @param config Configuration provider
     */
    explicit CredentialCacheSearcher(OathDeviceManager *deviceManager,
                                     OathDatabase *database,
                                     DaemonConfiguration *config);

    /**
     * @brief Finds device ID for cached credential when device is offline
     * @param credentialName Credential name to search for
     * @param deviceIdHint Optional device ID hint to check first
     * @return Device ID if found in cache, std::nullopt otherwise
     *
     * Search algorithm:
     * 1. Check if credentials cache is enabled in configuration
     * 2. If deviceIdHint provided: search only that device (if offline)
     * 3. Otherwise: search all offline devices in database
     * 4. Return first matching device ID or nullopt if not found
     *
     * @note Only searches offline devices (skips connected devices)
     * @note Returns immediately if cache is disabled in configuration
     */
    std::optional<QString> findCachedCredentialDevice(const QString &credentialName,
                                                      const QString &deviceIdHint = QString());

private:
    OathDeviceManager *m_deviceManager;
    OathDatabase *m_database;
    DaemonConfiguration *m_config;
};

} // namespace Daemon
} // namespace YubiKeyOath
