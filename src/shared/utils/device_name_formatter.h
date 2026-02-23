/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <optional>
#include "shared/types/device_model.h"

// Forward declaration
namespace YubiKeyOath {
namespace Daemon {
    class OathDatabase;
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
namespace DeviceNameFormatter
{

/**
 * @brief Generates default device name from model and serial number
 * @param deviceId Device ID (fallback if model unknown)
 * @param deviceModel Device model with brand and model string
 * @param serialNumber Device serial number (0 if unavailable)
 * @param database Database for checking duplicate names
 * @return Formatted name like "YubiKey 5C NFC - 12345678" or "Nitrokey 3C NFC - 56272111"
 *
 * Format rules:
 * - With serial: "{BRAND} {MODEL} - {SERIAL}" (e.g., "YubiKey 5C NFC - 12345678")
 * - Without serial (first): "{BRAND} {MODEL}" (e.g., "Nitrokey 3C NFC")
 * - Without serial (duplicate): "{BRAND} {MODEL} {N}" (e.g., "YubiKey 5C NFC 2")
 * - Unknown model: Falls back to device ID format "YubiKey (...4ccb10db)"
 *
 * @note Thread-safe: Database operations are thread-safe
 */
QString generateDefaultName(const QString &deviceId,
                            const DeviceModel& deviceModel,
                            quint32 serialNumber,
                            Daemon::OathDatabase *database);

/**
 * @brief Generates default device name from device ID (legacy fallback)
 * @param deviceId Device ID (hex string from YubiKey)
 * @return Formatted default name like "YubiKey (...4ccb10db)"
 *
 * Uses last 8 characters of device ID for shorter, more readable names.
 * Example: "28b5c0b54ccb10db" becomes "YubiKey (...4ccb10db)"
 *
 * @note Thread-safe: This is a pure function with no state
 * @note Legacy: Prefer using the overload with model/serial when available
 */
inline QString generateDefaultName(const QString &deviceId)
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
QString getDeviceDisplayName(const QString &deviceId,
                             Daemon::OathDatabase *database);

} // namespace DeviceNameFormatter

} // namespace Shared
} // namespace YubiKeyOath
