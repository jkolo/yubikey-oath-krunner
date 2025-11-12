/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "device_brand.h"
#include <QString>
#include <QStringList>
#include <cstdint>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Brand-agnostic device model representation
 *
 * This struct provides a unified way to represent device models from different
 * manufacturers (YubiKey, Nitrokey, etc.) while preserving brand-specific details.
 *
 * **Encoding scheme**:
 * - YubiKey models: `0xSSVVPPFF` (Series, Variant, Ports, Capabilities)
 * - Nitrokey models: `0xGGVVPPFF` (Generation, Variant, Ports, Capabilities)
 *
 * **Usage**:
 * ```cpp
 * DeviceModel model = detectYubiKeyModel(firmware, ...);
 * if (model.brand == DeviceBrand::YubiKey) {
 *     qDebug() << "YubiKey model:" << model.modelString;
 * }
 * ```
 */
struct DeviceModel {
    /**
     * @brief Device brand (YubiKey, Nitrokey, Unknown)
     */
    DeviceBrand brand{DeviceBrand::Unknown};

    /**
     * @brief Brand-specific model code encoding
     *
     * YubiKey: 0xSSVVPPFF
     * - SS: Series (YubiKeySeries enum)
     * - VV: Variant (YubiKeyVariant enum)
     * - PP: Ports (YubiKeyPorts bitfield)
     * - FF: Capabilities (YubiKeyCapabilities bitfield)
     *
     * Nitrokey: 0xGGVVPPFF
     * - GG: Generation (0x01=NK3A, 0x02=NK3C, 0x04=NK3AM, 0x05=NK3CM)
     * - VV: Variant (0x00=Standard, future: special editions)
     * - PP: Ports (same bitfield: USB_A=0x01, USB_C=0x02, NFC=0x08)
     * - FF: Capabilities (FIDO2=0x01, OATH=0x02, etc.)
     */
    quint32 modelCode{0x00000000};

    /**
     * @brief Human-readable model name
     *
     * Examples:
     * - "YubiKey 5C NFC"
     * - "Nitrokey 3C NFC"
     * - "Unknown Device"
     */
    QString modelString;

    /**
     * @brief Form factor code
     *
     * Values:
     * - 0: Unknown/unavailable
     * - 1: USB-A Keychain
     * - 2: USB-A Nano
     * - 3: USB-C Keychain
     * - 4: USB-C Nano
     * - 5: USB-C Lightning
     * - 6: USB-A Bio
     * - 7: USB-C Bio
     */
    quint8 formFactor{0};

    /**
     * @brief List of device capabilities
     *
     * Examples:
     * - ["FIDO2", "FIDO U2F", "OATH-HOTP", "OATH-TOTP", "PIV", "OpenPGP"]
     * - ["FIDO2", "OATH-HOTP", "OATH-TOTP"]
     */
    QStringList capabilities;

    /**
     * @brief Checks if device has NFC capability
     * @return true if NFC is supported
     */
    bool hasNFC() const {
        // NFC is encoded in ports byte (0x08 bit in PP byte)
        return (modelCode & 0x00000800) != 0;
    }

    /**
     * @brief Checks if device supports OATH (HOTP/TOTP)
     * @return true if OATH is supported
     */
    bool supportsOATH() const {
        // Check if "OATH-HOTP" or "OATH-TOTP" in capabilities
        for (const QString& cap : capabilities) {
            if (cap.contains(QStringLiteral("OATH"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Checks if device supports FIDO2
     * @return true if FIDO2 is supported
     */
    bool supportsFIDO2() const {
        // Check if "FIDO2" in capabilities
        for (const QString& cap : capabilities) {
            if (cap.contains(QStringLiteral("FIDO2"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Checks if device supports FIDO U2F
     * @return true if FIDO U2F is supported
     */
    bool supportsFIDO_U2F() const {
        // Check if "FIDO U2F" in capabilities
        for (const QString& cap : capabilities) {
            if (cap.contains(QStringLiteral("FIDO U2F"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Checks if device is FIPS certified
     * @return true if FIPS certified (YubiKey only)
     */
    bool isFIPS() const {
        // FIPS is encoded in series byte for YubiKey
        // Check modelString for "FIPS" keyword as fallback
        return modelString.contains(QStringLiteral("FIPS"), Qt::CaseInsensitive);
    }

    /**
     * @brief Checks if this is an unknown/undetected device
     * @return true if brand is Unknown or modelCode is 0
     */
    bool isUnknown() const {
        return brand == DeviceBrand::Unknown || modelCode == 0x00000000;
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const DeviceModel& other) const {
        return brand == other.brand && modelCode == other.modelCode;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const DeviceModel& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Converts model code to human-readable string (brand-aware)
 *
 * This function detects the device brand from the model code and converts
 * it to the appropriate human-readable string.
 *
 * @param modelCode Encoded model code from any supported brand
 * @return String like "YubiKey 5C NFC", "Nitrokey 3C NFC", or "Unknown Device"
 *
 * Examples:
 * - 0x01000AFF → "YubiKey 5C NFC"
 * - 0x02000A0F → "Nitrokey 3C NFC"
 */
QString deviceModelToString(quint32 modelCode);

} // namespace Shared
} // namespace YubiKeyOath
