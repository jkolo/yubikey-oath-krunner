/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_name_formatter.h"
#include "storage/yubikey_database.h"
#include "types/yubikey_model.h"

namespace YubiKeyOath {
namespace Shared {

QString DeviceNameFormatter::generateDefaultName(const QString &deviceId,
                                                  YubiKeyModel model,
                                                  quint32 serialNumber,
                                                  Daemon::YubiKeyDatabase *database)
{
    // If model is unknown (0), fall back to device ID format
    if (model == 0) {
        return generateDefaultName(deviceId);
    }

    // Convert model to human-readable string
    QString const modelString = modelToString(model);

    // If model string is empty or "Unknown", fall back to device ID format
    if (modelString.isEmpty() || modelString == QStringLiteral("Unknown")) {
        return generateDefaultName(deviceId);
    }

    // Remove "YubiKey " prefix if present (we'll add it back ourselves)
    QString cleanModelString = modelString;
    if (cleanModelString.startsWith(QStringLiteral("YubiKey "))) {
        cleanModelString = cleanModelString.mid(8); // Remove "YubiKey " (8 characters)
    }

    // Format with serial number if available
    if (serialNumber > 0) {
        return QStringLiteral("YubiKey %1 - %2").arg(cleanModelString).arg(serialNumber);
    }

    // Without serial number: check for duplicates and add counter if needed
    QString const baseName = QStringLiteral("YubiKey %1").arg(cleanModelString);

    // Count existing devices with this name prefix
    int const existingCount = database->countDevicesWithNamePrefix(baseName);

    if (existingCount == 0) {
        // First device with this model name
        return baseName; // NOLINT(performance-no-automatic-move)
    }

    // Duplicate model name - add counter
    return QStringLiteral("%1 %2").arg(baseName).arg(existingCount + 1);
}

QString DeviceNameFormatter::getDeviceDisplayName(const QString &deviceId,
                                                   Daemon::YubiKeyDatabase *database)
{
    // Try to get custom name from database
    auto deviceRecord = database->getDevice(deviceId);
    if (deviceRecord.has_value() && !deviceRecord->deviceName.isEmpty()) {
        return deviceRecord->deviceName;
    }

    // Fall back to generated default name
    return generateDefaultName(deviceId);
}

} // namespace Shared
} // namespace YubiKeyOath
