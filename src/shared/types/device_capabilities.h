/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QtGlobal>
#include "device_brand.h"
#include "../utils/version.h"

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief OATH protocol capabilities and behavioral differences
 *
 * Describes brand-specific protocol variations and capabilities.
 * Used to adapt protocol flow for different OATH device implementations.
 *
 * Key Differences:
 * - YubiKey: CALCULATE_ALL available, touch=0x6985, LIST has spurious errors
 * - Nitrokey: CALCULATE_ALL may be unavailable, touch=0x6982, LIST works reliably
 */
struct DeviceCapabilities {
    /**
     * @brief CALCULATE_ALL (INS=0xA4) command support
     *
     * YubiKey: Always true (all models support CALCULATE_ALL)
     * Nitrokey: Auto-detected at runtime (feature-gated in firmware)
     *
     * Detection: Send CALCULATE_ALL, check for 0x6D00 (INS_NOT_SUPPORTED)
     */
    bool supportsCalculateAll = true;

    /**
     * @brief Serial number in OATH SELECT response
     *
     * YubiKey: false (uses Management/PIV APIs for serial)
     * Nitrokey: true (TAG_SERIAL_NUMBER 0x8F in SELECT)
     */
    bool hasSelectSerial = false;

    /**
     * @brief Prefer LIST over CALCULATE_ALL
     *
     * YubiKey: false (CALCULATE_ALL avoids LIST spurious touch errors)
     * Nitrokey: true (LIST works reliably, CALCULATE_ALL may be unavailable)
     */
    bool preferList = false;

    /**
     * @brief Touch requirement status word
     *
     * YubiKey: 0x6985 (ConditionsNotSatisfied)
     * Nitrokey: 0x6982 (SecurityStatusNotSatisfied)
     *
     * Both indicate that credential requires physical touch before generating code.
     */
    quint16 touchRequiredStatusWord = 0x6985;

    /**
     * @brief Detects capabilities from brand and firmware
     * @param brand Device brand (YubiKey, Nitrokey, etc.)
     * @param firmware Firmware version from OATH SELECT
     * @return Capabilities struct with brand-specific defaults
     *
     * Note: For Nitrokey, supportsCalculateAll must be verified at runtime
     */
    static DeviceCapabilities detect(DeviceBrand brand, const Version& firmware);

    /**
     * @brief Checks if status word indicates touch requirement
     * @param statusWord APDU status word (SW1 SW2)
     * @return true if statusWord matches this device's touch requirement code
     */
    bool isTouchRequired(quint16 statusWord) const;
};

} // namespace Shared
} // namespace YubiKeyOath
