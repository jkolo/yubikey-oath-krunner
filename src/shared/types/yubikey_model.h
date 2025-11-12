/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "device_model.h"
#include <QString>
#include <QFlags>
#include <cstdint>

class QDBusArgument;

namespace YubiKeyOath {
namespace Shared {

class Version;  // Forward declaration

/**
 * @brief YubiKey Series (Main Product Lines)
 *
 * Encoding: Byte 0 (SS) of 0xSSVVPPFF
 */
enum class YubiKeySeries : uint8_t {
    Unknown = 0x00,      ///< Unknown or undetected model
    YubiKey5 = 0x01,     ///< YubiKey 5 Series (full-featured flagship)
    YubiKey5FIPS = 0x02, ///< YubiKey 5 FIPS Series (FIPS 140-2 certified)
    YubiKeyBio = 0x03,   ///< YubiKey Bio Series (fingerprint authentication)
    SecurityKey = 0x04,  ///< Security Key Series (FIDO-only, budget line)
    YubiKeyNEO = 0x10,   ///< YubiKey NEO (legacy, first NFC-enabled)
    YubiKey4 = 0x11,     ///< YubiKey 4 Series (legacy, pre-FIDO2)
    YubiKey4FIPS = 0x12  ///< YubiKey 4 FIPS Series (legacy, FIPS certified)
};

/**
 * @brief YubiKey Variant (Form Factor + Special Features)
 *
 * Encoding: Byte 1 (VV) of 0xSSVVPPFF
 */
enum class YubiKeyVariant : uint8_t {
    Standard = 0x00,      ///< Standard keychain size
    Nano = 0x01,          ///< Ultra-compact, stay-in-port (1/3 size)
    DualConnector = 0x02, ///< Dual connector (5Ci only - USB-C + Lightning)
    EnhancedPIN = 0x10    ///< Enhanced PIN firmware variant (subscription only)
};

/**
 * @brief YubiKey Port (Physical Hardware Interfaces)
 *
 * Encoding: Byte 2 (PP) of 0xSSVVPPFF (bitfield - can have multiple)
 */
enum YubiKeyPort : uint8_t {
    NoPort = 0x00,   ///< No port (invalid)
    USB_A = 0x01,    ///< USB-A (traditional rectangular USB)
    USB_C = 0x02,    ///< USB-C (modern reversible USB)
    Lightning = 0x04, ///< Lightning (Apple iOS connector)
    NFC = 0x08       ///< NFC (Near-Field Communication wireless)
};
Q_DECLARE_FLAGS(YubiKeyPorts, YubiKeyPort)
Q_DECLARE_OPERATORS_FOR_FLAGS(YubiKeyPorts)

/**
 * @brief YubiKey Capability (Protocol Support)
 *
 * Encoding: Byte 3 (FF) of 0xSSVVPPFF (bitfield - can have multiple)
 */
enum YubiKeyCapability : uint8_t {
    NoCapability = 0x00, ///< No capability (invalid)
    FIDO2 = 0x01,        ///< FIDO2/WebAuthn (modern passwordless)
    FIDO_U2F = 0x02,     ///< FIDO U2F (legacy 2FA)
    YubicoOTP = 0x04,    ///< Yubico OTP (proprietary OTP via HID keyboard)
    OATH_HOTP = 0x08,    ///< OATH-HOTP (counter-based OTP, RFC 4226)
    OATH_TOTP = 0x10,    ///< OATH-TOTP (time-based OTP, RFC 6238)
    PIV = 0x20,          ///< PIV (Smart Card, NIST SP 800-73-4)
    OpenPGP = 0x40,      ///< OpenPGP (email encryption/signing)
    HMAC_SHA1 = 0x80     ///< HMAC-SHA1 Challenge-Response
};
Q_DECLARE_FLAGS(YubiKeyCapabilities, YubiKeyCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(YubiKeyCapabilities)

/**
 * @brief YubiKey Model - Encoded as single uint32
 *
 * Encoding: 0xSSVVPPFF
 * - SS: Series (YubiKeySeries)
 * - VV: Variant (YubiKeyVariant)
 * - PP: Ports (YubiKeyPorts bitfield)
 * - FF: Capabilities (YubiKeyCapabilities bitfield)
 *
 * Examples:
 * - YubiKey 5C NFC FIPS = 0x02000AFF (Series=5 FIPS, Variant=Std, Ports=C+NFC, Caps=All)
 * - YubiKey NEO = 0x100009FE (Series=NEO, Variant=Std, Ports=A+NFC, Caps=No FIDO2)
 * - YubiKey Bio USB-C = 0x03000203 (Series=Bio, Variant=Std, Ports=C, Caps=FIDO2+U2F)
 */
using YubiKeyModel = uint32_t;

/**
 * @brief Extracts Series from encoded YubiKeyModel
 * @param model Encoded model (0xSSVVPPFF)
 * @return Series enum value
 */
YubiKeySeries getModelSeries(YubiKeyModel model);

/**
 * @brief Extracts Variant from encoded YubiKeyModel
 * @param model Encoded model (0xSSVVPPFF)
 * @return Variant enum value
 */
YubiKeyVariant getModelVariant(YubiKeyModel model);

/**
 * @brief Extracts Ports from encoded YubiKeyModel
 * @param model Encoded model (0xSSVVPPFF)
 * @return Ports bitfield
 */
YubiKeyPorts getModelPorts(YubiKeyModel model);

/**
 * @brief Extracts Capabilities from encoded YubiKeyModel
 * @param model Encoded model (0xSSVVPPFF)
 * @return Capabilities bitfield
 */
YubiKeyCapabilities getModelCapabilities(YubiKeyModel model);

/**
 * @brief Converts YubiKeyModel to human-readable string
 * @param model Encoded model
 * @return String like "YubiKey 5C NFC FIPS" or "YubiKey NEO"
 */
QString modelToString(YubiKeyModel model);

/**
 * @brief Checks if model has NFC support
 * @param model Encoded model
 * @return true if NFC port is present
 */
bool hasNFC(YubiKeyModel model);

/**
 * @brief Checks if model is FIPS certified
 * @param model Encoded model
 * @return true if Series is FIPS variant (YubiKey5FIPS or YubiKey4FIPS)
 */
bool isFIPS(YubiKeyModel model);

/**
 * @brief Checks if model supports OATH (HOTP/TOTP)
 * @param model Encoded model
 * @return true if OATH_HOTP or OATH_TOTP capability is present
 */
bool supportsOATH(YubiKeyModel model);

/**
 * @brief Converts capabilities bitfield to list of human-readable strings
 * @param capabilities Capabilities bitfield
 * @return List of capability names (e.g., ["FIDO2", "OATH-TOTP", "PIV"])
 */
QStringList capabilitiesToStringList(YubiKeyCapabilities capabilities);

/**
 * @brief Converts form factor byte to human-readable string
 * @param formFactor Form factor code (0x00-0x07)
 * @return Human-readable form factor string (e.g., "USB-A Keychain", "USB-C NFC")
 */
QString formFactorToString(quint8 formFactor);

/**
 * @brief Detects YubiKey model from firmware version, optional ykman output, form factor, and NFC support
 * @param firmware Firmware version from OATH TAG_VERSION or Management Interface
 * @param ykmanOutput Output from "ykman list" (e.g., "YubiKey 5C NFC (5.4.3) [OTP+FIDO+CCID]")
 * @param formFactor FormFactor from Management Interface GET_DEVICE_INFO (0x00=unknown, 0x01=USB-A, 0x03=USB-C, etc.)
 * @param nfcSupported NFC interfaces supported from Management Interface (2-byte bitfield, 0x0000=no NFC, non-zero=NFC present)
 * @return Detected model as encoded uint32, or 0x00000000 (Unknown) if detection fails
 *
 * Detection algorithm:
 * 1. If ykmanOutput available: parse model name, detect ports from name, detect capabilities from brackets
 * 2. If ykmanOutput unavailable: fallback to firmware version ranges for series detection
 *    - Use formFactor to determine USB port type (USB-A vs USB-C) if available
 *    - Use nfcSupported to add NFC port if device has NFC capability
 * 3. Combine series + variant + ports + capabilities into single uint32
 */
YubiKeyModel detectYubiKeyModel(const Version& firmware, const QString& ykmanOutput = QString(), quint8 formFactor = 0, quint16 nfcSupported = 0);

/**
 * @brief Converts YubiKeyModel to brand-agnostic DeviceModel
 * @param ykModel Encoded YubiKey model (0xSSVVPPFF)
 * @return DeviceModel struct with brand=YubiKey, modelCode, modelString, capabilities
 *
 * This function converts the YubiKey-specific encoded model to the generic DeviceModel
 * structure used throughout the application for brand-agnostic device handling.
 *
 * Example:
 * ```cpp
 * YubiKeyModel ykModel = detectYubiKeyModel(firmware, ...);
 * DeviceModel model = toDeviceModel(ykModel);
 * qDebug() << "Model:" << model.modelString; // "YubiKey 5C NFC"
 * ```
 */
DeviceModel toDeviceModel(YubiKeyModel ykModel);

/**
 * @brief Creates YubiKeyModel from components
 * @param series Series enum
 * @param variant Variant enum
 * @param ports Ports bitfield
 * @param capabilities Capabilities bitfield
 * @return Encoded model (0xSSVVPPFF)
 */
YubiKeyModel createModel(YubiKeySeries series, YubiKeyVariant variant,
                         YubiKeyPorts ports, YubiKeyCapabilities capabilities);

} // namespace Shared
} // namespace YubiKeyOath
