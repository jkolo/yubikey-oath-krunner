/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <optional>

// Forward declaration
namespace YubiKeyOath {
namespace Daemon {
    class YubiKeyDatabase;
}
}

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Utility for formatting YubiKey device names
 *
 * Single Responsibility: Generate consistent default device names
 * across the application.
 */
class DeviceNameFormatter
{
public:
    /**
     * @brief Generates default device name from device ID
     * @param deviceId Device ID (hex string from YubiKey)
     * @return Formatted default name like "YubiKey (...4ccb10db)"
     *
     * Uses last 8 characters of device ID for shorter, more readable names.
     * Example: "28b5c0b54ccb10db" becomes "YubiKey (...4ccb10db)"
     *
     * @note Thread-safe: This is a pure static function with no state
     */
    static QString generateDefaultName(const QString &deviceId)
    {
        if (deviceId.length() > 8) {
            const QString shortId = deviceId.right(8);
            return QStringLiteral("YubiKey (...") + shortId + QStringLiteral(")");
        }
        return QStringLiteral("YubiKey (") + deviceId + QStringLiteral(")");
    }

    /**
     * @brief Gets device display name (custom from database or generated default)
     * @param deviceId Device ID to get name for
     * @param database Database to query for custom name
     * @return Custom name from database if set, otherwise generated default name
     *
     * This method consolidates the common pattern of:
     * 1. Try to get custom name from database
     * 2. If not found or empty, generate default name
     *
     * @note Thread-safe: Database operations are thread-safe
     *
     * @par Usage Example
     * @code
     * QString name = DeviceNameFormatter::getDeviceDisplayName(deviceId, database);
     * // Returns "My YubiKey" if set in database, or "YubiKey (...4ccb10db)" otherwise
     * @endcode
     */
    static QString getDeviceDisplayName(const QString &deviceId,
                                        Daemon::YubiKeyDatabase *database);
};

} // namespace Shared
} // namespace YubiKeyOath
