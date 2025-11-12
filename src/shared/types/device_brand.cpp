/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_brand.h"

#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {

DeviceBrand detectBrand(const QString& readerName,
                       const Version& firmware,
                       bool hasSelectSerial)
{
    // Strategy #1: Reader name pattern matching (highest priority)
    // Most reliable and fastest method
    if (readerName.contains(QStringLiteral("Nitrokey"), Qt::CaseInsensitive)) {
        return DeviceBrand::Nitrokey;
    }

    if (readerName.contains(QStringLiteral("Yubico"), Qt::CaseInsensitive) ||
        readerName.contains(QStringLiteral("YubiKey"), Qt::CaseInsensitive)) {
        return DeviceBrand::YubiKey;
    }

    // Strategy #2: Serial number location + firmware version
    // Nitrokey 3: Has TAG_SERIAL_NUMBER (0x8F) in SELECT, firmware 4.14.0+
    // YubiKey: No TAG_SERIAL_NUMBER in SELECT (uses Management/PIV APIs)
    if (hasSelectSerial) {
        // Nitrokey 3 started with firmware 4.14.0
        if (firmware >= Version(4, 14, 0)) {
            return DeviceBrand::Nitrokey;
        }
    }

    // Strategy #3: Firmware version heuristics
    // YubiKey 5: Firmware 5.x.x without TAG_SERIAL_NUMBER
    if (firmware.major() == 5 && !hasSelectSerial) {
        return DeviceBrand::YubiKey;
    }

    // YubiKey 4/NEO: Firmware < 5 without TAG_SERIAL_NUMBER
    if (firmware.major() < 5 && !hasSelectSerial) {
        return DeviceBrand::YubiKey;
    }

    // Conservative fallback: Assume YubiKey for unknown devices
    // This maintains backward compatibility and is safe default
    return DeviceBrand::YubiKey;
}

QString brandName(DeviceBrand brand)
{
    switch (brand) {
    case DeviceBrand::YubiKey:
        return i18nc("@label Device brand name", "YubiKey");
    case DeviceBrand::Nitrokey:
        return i18nc("@label Device brand name", "Nitrokey");
    case DeviceBrand::Unknown:
    default:
        return i18nc("@label Unknown device brand", "Unknown");
    }
}

QString brandPrefix(DeviceBrand brand)
{
    switch (brand) {
    case DeviceBrand::YubiKey:
        return QStringLiteral("yubikey");
    case DeviceBrand::Nitrokey:
        return QStringLiteral("nitrokey");
    case DeviceBrand::Unknown:
    default:
        return QStringLiteral("oath-device");
    }
}

bool isBrandSupported(DeviceBrand brand)
{
    return brand == DeviceBrand::YubiKey ||
           brand == DeviceBrand::Nitrokey;
}

DeviceBrand detectBrandFromModelString(const QString& modelString)
{
    // Simple pattern matching on model string
    if (modelString.contains(QStringLiteral("Nitrokey"), Qt::CaseInsensitive)) {
        return DeviceBrand::Nitrokey;
    }

    if (modelString.contains(QStringLiteral("YubiKey"), Qt::CaseInsensitive)) {
        return DeviceBrand::YubiKey;
    }

    // Default to YubiKey for backward compatibility
    return DeviceBrand::YubiKey;
}

} // namespace Shared
} // namespace YubiKeyOath
