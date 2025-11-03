/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_name_formatter.h"
#include "storage/yubikey_database.h"

namespace YubiKeyOath {
namespace Shared {

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
