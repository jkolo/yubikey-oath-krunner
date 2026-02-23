/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_name_formatter.h"
#include "daemon/storage/oath_database.h"
#include "shared/types/device_model.h"
#include "shared/types/device_brand.h"

namespace YubiKeyOath {
namespace Shared {

namespace DeviceNameFormatter {

QString generateDefaultName(const QString &deviceId,
                            const DeviceModel& deviceModel,
                            quint32 serialNumber,
                            Daemon::OathDatabase *database)
{
    // If model brand is unknown, fall back to device ID format
    if (deviceModel.brand == DeviceBrand::Unknown) {
        return generateDefaultName(deviceId);
    }

    // If model string is empty or "Unknown", fall back to device ID format
    if (deviceModel.modelString.isEmpty() || deviceModel.modelString == QStringLiteral("Unknown")) {
        return generateDefaultName(deviceId);
    }

    // Use the model string directly (already formatted with brand, e.g., "YubiKey 5C NFC" or "Nitrokey 3C NFC")
    const QString& modelString = deviceModel.modelString;

    // Format with serial number if available
    if (serialNumber > 0) {
        return QStringLiteral("%1 - %2").arg(modelString).arg(serialNumber);
    }

    // Without serial number: check for duplicates and add counter if needed
    const QString baseName = modelString;

    // Count existing devices with this name prefix
    int const existingCount = database->countDevicesWithNamePrefix(baseName);

    if (existingCount == 0) {
        // First device with this model name
        return baseName; // NOLINT(performance-no-automatic-move)
    }

    // Duplicate model name - add counter
    return QStringLiteral("%1 %2").arg(baseName).arg(existingCount + 1);
}

QString getDeviceDisplayName(const QString &deviceId,
                             Daemon::OathDatabase *database)
{
    // Try to get custom name from database
    auto deviceRecord = database->getDevice(deviceId);
    if (deviceRecord.has_value() && !deviceRecord->deviceName.isEmpty()) {
        return deviceRecord->deviceName;
    }

    // Fall back to generated default name
    return generateDefaultName(deviceId);
}

} // namespace DeviceNameFormatter

} // namespace Shared
} // namespace YubiKeyOath
