/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_capabilities.h"

namespace YubiKeyOath {
namespace Shared {

DeviceCapabilities DeviceCapabilities::detect(DeviceBrand brand, const Version& firmware)
{
    Q_UNUSED(firmware);  // May be used in future for version-specific capabilities

    DeviceCapabilities caps;

    switch (brand) {
    case DeviceBrand::YubiKey:
        // YubiKey defaults
        caps.supportsCalculateAll = true;   // All YubiKeys support CALCULATE_ALL
        caps.hasSelectSerial = false;        // Serial via Management/PIV, not SELECT
        caps.preferList = false;             // LIST has spurious touch errors
        caps.touchRequiredStatusWord = 0x6985;  // ConditionsNotSatisfied
        break;

    case DeviceBrand::Nitrokey:
        // Nitrokey 3 defaults
        caps.supportsCalculateAll = false;   // Feature-gated, test at runtime
        caps.hasSelectSerial = true;         // TAG_SERIAL_NUMBER in SELECT
        caps.preferList = true;              // LIST works reliably
        caps.touchRequiredStatusWord = 0x6982;  // SecurityStatusNotSatisfied
        break;

    case DeviceBrand::Unknown:
    default:
        // Conservative defaults for unknown devices
        // Assume YubiKey-compatible behavior
        caps.supportsCalculateAll = true;
        caps.hasSelectSerial = false;
        caps.preferList = false;
        caps.touchRequiredStatusWord = 0x6985;
        break;
    }

    return caps;
}

bool DeviceCapabilities::isTouchRequired(quint16 statusWord) const
{
    // Support both YubiKey (0x6985) and Nitrokey (0x6982) status words
    // This allows clients to check touch requirement regardless of brand
    return statusWord == 0x6985 || statusWord == 0x6982;
}

} // namespace Shared
} // namespace YubiKeyOath
