/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "shared/types/device_model.h"
#include "shared/utils/version.h"
#include <QString>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Nitrokey model detection from reader name and firmware version
 *
 * This module provides Nitrokey-specific device model detection, converting
 * PC/SC reader names and firmware versions into structured DeviceModel objects.
 *
 * **Supported Models:**
 * - Nitrokey 3A Mini (USB-A, Mini form factor)
 * - Nitrokey 3A NFC (USB-A, NFC-enabled)
 * - Nitrokey 3C NFC (USB-C, NFC-enabled)
 * - Nitrokey 3C Mini (USB-C, Mini form factor) [Future]
 *
 * **Reader Name Parsing:**
 * PC/SC reader names contain model information:
 * - Format: "Nitrokey Nitrokey 3 [CCID/ICCD Interface]"
 * - Variant detection from firmware/serial presence
 *
 * **Model Code Encoding (0xGGVVPPFF):**
 * - GG: Generation (0x01=NK3A, 0x02=NK3C, 0x04=NK3AM, 0x05=NK3CM)
 * - VV: Variant (0x00=Standard, future: special editions)
 * - PP: Ports (USB_A=0x01, USB_C=0x02, NFC=0x08)
 * - FF: Capabilities (FIDO2=0x01, OATH=0x02, OpenPGP=0x04, PIV=0x08)
 *
 * **Usage Example:**
 * ```cpp
 * QString readerName = "Nitrokey Nitrokey 3 [CCID/ICCD Interface]";
 * Version firmware(1, 6, 0);
 * quint32 serial = 562721119;
 *
 * DeviceModel model = detectNitrokeyModel(readerName, firmware, serial);
 * // Returns: DeviceModel {
 * //   brand = DeviceBrand::Nitrokey,
 * //   modelCode = 0x02000A02,  // 3C NFC (USB-C + NFC + OATH)
 * //   modelString = "Nitrokey 3C NFC",
 * //   capabilities = ["FIDO2", "OATH-HOTP", "OATH-TOTP", "OpenPGP"]
 * // }
 * ```
 */

/**
 * @brief Detects Nitrokey model from reader name and firmware
 * @param readerName PC/SC reader name (contains model info)
 * @param firmware Firmware version from SELECT command
 * @param serialNumber Optional serial number (helps variant detection)
 * @return DeviceModel with brand=Nitrokey and detected model
 *
 * Detection strategy:
 * 1. Parse reader name for "Nitrokey 3" presence
 * 2. Determine USB variant (A vs C) from firmware/serial patterns
 * 3. Detect NFC capability from firmware features
 * 4. Construct modelCode with generation + ports + capabilities
 * 5. Generate human-readable modelString
 *
 * Fallback: If detection fails, returns DeviceModel with brand=Unknown
 */
Shared::DeviceModel detectNitrokeyModel(const QString &readerName,
                                        const Shared::Version &firmware,
                                        quint32 serialNumber = 0);

} // namespace Daemon
} // namespace YubiKeyOath
