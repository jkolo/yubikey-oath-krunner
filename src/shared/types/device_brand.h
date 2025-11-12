/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include "../utils/version.h"

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Supported OATH device brands
 *
 * Identifies the manufacturer/brand of OATH-compatible devices.
 * Used for brand-specific protocol variations and UI customization.
 */
enum class DeviceBrand : uint8_t {
    Unknown = 0x00,  ///< Unknown or undetected brand
    YubiKey = 0x01,  ///< Yubico YubiKey (NEO, 4, 5, Bio, Security Key)
    Nitrokey = 0x02  ///< Nitrokey 3 series (3A, 3C, Mini)
    // Future: SoloKey = 0x03, OnlyKey = 0x04, etc.
};

/**
 * @brief Detects device brand from multiple sources
 *
 * Detection strategies (in order of priority):
 * 1. Reader name pattern matching (fastest, most reliable)
 * 2. Serial number location + firmware version
 * 3. Conservative fallback to YubiKey
 *
 * @param readerName PC/SC reader name (e.g., "Yubico YubiKey OTP+FIDO+CCID")
 * @param firmware Firmware version from OATH SELECT TAG_VERSION (0x79)
 * @param hasSelectSerial True if TAG_SERIAL_NUMBER (0x8F) present in SELECT response
 * @return Detected brand (never returns Unknown - defaults to YubiKey)
 */
DeviceBrand detectBrand(const QString& readerName,
                       const Version& firmware,
                       bool hasSelectSerial);

/**
 * @brief Gets human-readable brand name
 * @param brand Device brand
 * @return Brand name ("YubiKey", "Nitrokey", "Unknown")
 */
QString brandName(DeviceBrand brand);

/**
 * @brief Gets brand prefix for icon paths
 * @param brand Device brand
 * @return Lowercase brand prefix ("yubikey", "nitrokey", "oath-device")
 */
QString brandPrefix(DeviceBrand brand);

/**
 * @brief Checks if brand is known/supported
 * @param brand Device brand
 * @return true if brand is YubiKey or Nitrokey, false if Unknown
 */
bool isBrandSupported(DeviceBrand brand);

/**
 * @brief Detects device brand from model string
 *
 * Simple pattern matching on model string for UI purposes.
 * Used in config module where reader name is not available.
 *
 * @param modelString Human-readable model (e.g., "Nitrokey 3C NFC", "YubiKey 5C NFC")
 * @return Detected brand (defaults to YubiKey if no match)
 */
DeviceBrand detectBrandFromModelString(const QString& modelString);

} // namespace Shared
} // namespace YubiKeyOath
