/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_model.h"
#include "../utils/version.h"
#include <QDBusArgument>
#include <QStringList>
#include <QRegularExpression>

namespace YubiKeyOath {
namespace Shared {

// ==================== Helper Functions ====================

YubiKeySeries getModelSeries(YubiKeyModel model)
{
    return static_cast<YubiKeySeries>((model >> 24) & 0xFF);
}

YubiKeyVariant getModelVariant(YubiKeyModel model)
{
    return static_cast<YubiKeyVariant>((model >> 16) & 0xFF);
}

YubiKeyPorts getModelPorts(YubiKeyModel model)
{
    return {static_cast<YubiKeyPort>((model >> 8) & 0xFF)};
}

YubiKeyCapabilities getModelCapabilities(YubiKeyModel model)
{
    return {static_cast<YubiKeyCapability>(model & 0xFF)};
}

bool hasNFC(YubiKeyModel model)
{
    return getModelPorts(model).testFlag(YubiKeyPort::NFC);
}

bool isFIPS(YubiKeyModel model)
{
    auto series = getModelSeries(model);
    return series == YubiKeySeries::YubiKey5FIPS || series == YubiKeySeries::YubiKey4FIPS;
}

bool supportsOATH(YubiKeyModel model)
{
    auto caps = getModelCapabilities(model);
    return caps.testFlag(YubiKeyCapability::OATH_HOTP) || caps.testFlag(YubiKeyCapability::OATH_TOTP);
}

YubiKeyModel createModel(YubiKeySeries series, YubiKeyVariant variant,
                         YubiKeyPorts ports, YubiKeyCapabilities capabilities)
{
    return (static_cast<uint32_t>(series) << 24) |
           (static_cast<uint32_t>(variant) << 16) |
           (static_cast<uint32_t>(ports) << 8) |
           static_cast<uint32_t>(capabilities);
}

// ==================== Model to String Conversion ====================

QString modelToString(YubiKeyModel model)
{
    auto series = getModelSeries(model);
    auto variant = getModelVariant(model);
    auto ports = getModelPorts(model);

    // Base model name from series
    QString name;
    switch (series) {
    case YubiKeySeries::YubiKey5:
    case YubiKeySeries::YubiKey5FIPS:
        name = QStringLiteral("YubiKey 5");
        break;
    case YubiKeySeries::YubiKeyBio:
        name = QStringLiteral("YubiKey Bio");
        break;
    case YubiKeySeries::SecurityKey:
        name = QStringLiteral("Security Key");
        break;
    case YubiKeySeries::YubiKeyNEO:
        return QStringLiteral("YubiKey NEO");  // NEO has no variants
    case YubiKeySeries::YubiKey4:
    case YubiKeySeries::YubiKey4FIPS:
        name = QStringLiteral("YubiKey 4");
        break;
    default:
        return QStringLiteral("Unknown YubiKey");
    }

    // Add connector info
    if (variant == YubiKeyVariant::DualConnector) {
        // 5Ci - dual connector USB-C + Lightning
        name += QStringLiteral("Ci");
    } else {
        // Regular models - add connector suffix
        bool const hasUSB_C = ports.testFlag(YubiKeyPort::USB_C);
        bool const hasUSB_A = ports.testFlag(YubiKeyPort::USB_A);
        bool const hasNFCPort = ports.testFlag(YubiKeyPort::NFC);

        if (hasUSB_C && !hasUSB_A) {
            name += QStringLiteral("C");  // YubiKey 5C, YubiKey 4C
        }

        if (hasNFCPort) {
            name += QStringLiteral(" NFC");  // YubiKey 5 NFC, YubiKey 5C NFC
        }
    }

    // Add variant suffix
    if (variant == YubiKeyVariant::Nano) {
        name += QStringLiteral(" Nano");
    } else if (variant == YubiKeyVariant::EnhancedPIN) {
        name += QStringLiteral(" Enhanced PIN");
    }

    // Add FIPS suffix
    if (series == YubiKeySeries::YubiKey5FIPS || series == YubiKeySeries::YubiKey4FIPS) {
        name += QStringLiteral(" FIPS");
    }

    return name;
}

// ==================== Model Detection ====================

namespace {

/**
 * @brief Parses model name from ykman output
 *
 * Examples:
 * - "YubiKey 5C NFC (5.4.3) [OTP+FIDO+CCID]"
 * - "YubiKey NEO (3.4.0) [OTP+FIDO+CCID]"
 * - "Security Key NFC by Yubico"
 */
struct YkmanParseResult {
    YubiKeySeries series = YubiKeySeries::Unknown;
    YubiKeyVariant variant = YubiKeyVariant::Standard;
    YubiKeyPorts ports = YubiKeyPort::NoPort;
    YubiKeyCapabilities capabilities = YubiKeyCapability::NoCapability;
    bool valid = false;
};

YkmanParseResult parseYkmanOutput(const QString& ykmanOutput)
{
    YkmanParseResult result;

    if (ykmanOutput.isEmpty()) {
        return result;
    }

    QString const line = ykmanOutput.trimmed();

    // Detect Series
    if (line.contains(QStringLiteral("YubiKey 5"), Qt::CaseInsensitive)) {
        if (line.contains(QStringLiteral("FIPS"), Qt::CaseInsensitive)) {
            result.series = YubiKeySeries::YubiKey5FIPS;
        } else {
            result.series = YubiKeySeries::YubiKey5;
        }
    } else if (line.contains(QStringLiteral("YubiKey Bio"), Qt::CaseInsensitive)) {
        result.series = YubiKeySeries::YubiKeyBio;
    } else if (line.contains(QStringLiteral("Security Key"), Qt::CaseInsensitive)) {
        result.series = YubiKeySeries::SecurityKey;
    } else if (line.contains(QStringLiteral("YubiKey NEO"), Qt::CaseInsensitive)) {
        result.series = YubiKeySeries::YubiKeyNEO;
    } else if (line.contains(QStringLiteral("YubiKey 4"), Qt::CaseInsensitive)) {
        if (line.contains(QStringLiteral("FIPS"), Qt::CaseInsensitive)) {
            result.series = YubiKeySeries::YubiKey4FIPS;
        } else {
            result.series = YubiKeySeries::YubiKey4;
        }
    } else {
        return result;  // Unknown series
    }

    // Detect Variant
    if (line.contains(QStringLiteral("Nano"), Qt::CaseInsensitive)) {
        result.variant = YubiKeyVariant::Nano;
    } else if (line.contains(QStringLiteral("Ci"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("5Ci"), Qt::CaseInsensitive)) {
        result.variant = YubiKeyVariant::DualConnector;
    } else if (line.contains(QStringLiteral("Enhanced PIN"), Qt::CaseInsensitive)) {
        result.variant = YubiKeyVariant::EnhancedPIN;
    } else {
        result.variant = YubiKeyVariant::Standard;
    }

    // Detect Ports from model name
    if (line.contains(QStringLiteral("5Ci"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("Ci"), Qt::CaseInsensitive)) {
        result.ports = YubiKeyPort::USB_C | YubiKeyPort::Lightning;
    } else if (line.contains(QStringLiteral("5C"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("4C"), Qt::CaseInsensitive)) {
        result.ports = YubiKeyPort::USB_C;
    } else {
        result.ports = YubiKeyPort::USB_A;  // Default to USB-A
    }

    if (line.contains(QStringLiteral("NFC"), Qt::CaseInsensitive)) {
        result.ports |= YubiKeyPort::NFC;
    }

    // Detect Capabilities from [OTP+FIDO+CCID] brackets
    // Default capabilities based on series if no brackets found
    switch (result.series) {
    case YubiKeySeries::YubiKey5:
    case YubiKeySeries::YubiKey5FIPS:
        // YubiKey 5 has all capabilities except fingerprint
        result.capabilities = YubiKeyCapability::FIDO2 |
                              YubiKeyCapability::FIDO_U2F |
                              YubiKeyCapability::YubicoOTP |
                              YubiKeyCapability::OATH_HOTP |
                              YubiKeyCapability::OATH_TOTP |
                              YubiKeyCapability::PIV |
                              YubiKeyCapability::OpenPGP |
                              YubiKeyCapability::HMAC_SHA1;
        break;

    case YubiKeySeries::YubiKeyBio:
    case YubiKeySeries::SecurityKey:
        // Bio and Security Key: FIDO2 + U2F only
        result.capabilities = YubiKeyCapability::FIDO2 | YubiKeyCapability::FIDO_U2F;
        break;

    case YubiKeySeries::YubiKeyNEO:
    case YubiKeySeries::YubiKey4:
    case YubiKeySeries::YubiKey4FIPS:
        // NEO and 4: All capabilities except FIDO2
        result.capabilities = YubiKeyCapability::FIDO_U2F |
                              YubiKeyCapability::YubicoOTP |
                              YubiKeyCapability::OATH_HOTP |
                              YubiKeyCapability::OATH_TOTP |
                              YubiKeyCapability::PIV |
                              YubiKeyCapability::OpenPGP |
                              YubiKeyCapability::HMAC_SHA1;
        break;

    default:
        result.capabilities = YubiKeyCapability::NoCapability;
        break;
    }

    result.valid = (result.series != YubiKeySeries::Unknown);
    return result;
}

/**
 * @brief Detects series from firmware version ranges
 *
 * Firmware version ranges (from Yubico documentation):
 * - YubiKey 5: 5.0.0 - 5.7.x+
 * - YubiKey 5 FIPS: 5.4.x - 5.7.x+ (same as YubiKey 5, but FIPS certified)
 * - YubiKey Bio: 5.5.x+
 * - YubiKey NEO: 3.0.0 - 3.x.x
 * - YubiKey 4: 4.0.0 - 4.x.x
 *
 * Note: Cannot distinguish FIPS from non-FIPS by firmware alone - need ykman output
 */
YubiKeySeries detectSeriesFromFirmware(const Version& firmware)
{
    if (!firmware.isValid()) {
        return YubiKeySeries::Unknown;
    }

    int const major = firmware.major();
    int const minor = firmware.minor();

    if (major == 5) {
        // YubiKey 5 series (cannot distinguish FIPS from firmware alone)
        if (minor >= 5) {
            // Could be YubiKey 5 or YubiKey Bio
            // Default to YubiKey 5 (Bio needs ykman output for reliable detection)
            return YubiKeySeries::YubiKey5;
        }
        return YubiKeySeries::YubiKey5;
    } else if (major == 4) {
        return YubiKeySeries::YubiKey4;
    } else if (major == 3) {
        return YubiKeySeries::YubiKeyNEO;
    }

    return YubiKeySeries::Unknown;
}

} // anonymous namespace

YubiKeyModel detectModel(const Version& firmware, const QString& ykmanOutput)
{
    // Try parsing ykman output first (most reliable)
    if (!ykmanOutput.isEmpty()) {
        auto result = parseYkmanOutput(ykmanOutput);
        if (result.valid) {
            return createModel(result.series, result.variant, result.ports, result.capabilities);
        }
    }

    // Fallback: detect series from firmware version
    auto series = detectSeriesFromFirmware(firmware);
    if (series == YubiKeySeries::Unknown) {
        return 0x00000000;  // Unknown model
    }

    // Default assumptions when no ykman output available
    YubiKeyVariant const variant = YubiKeyVariant::Standard;
    YubiKeyPorts ports = YubiKeyPort::USB_A;  // Assume USB-A
    YubiKeyCapabilities capabilities = YubiKeyCapability::NoCapability;

    // Set default capabilities based on series
    switch (series) {
    case YubiKeySeries::YubiKey5:
    case YubiKeySeries::YubiKey5FIPS:
        capabilities = YubiKeyCapability::FIDO2 |
                       YubiKeyCapability::FIDO_U2F |
                       YubiKeyCapability::YubicoOTP |
                       YubiKeyCapability::OATH_HOTP |
                       YubiKeyCapability::OATH_TOTP |
                       YubiKeyCapability::PIV |
                       YubiKeyCapability::OpenPGP |
                       YubiKeyCapability::HMAC_SHA1;
        break;

    case YubiKeySeries::YubiKeyNEO:
        ports = YubiKeyPort::USB_A | YubiKeyPort::NFC;  // NEO always has NFC
        capabilities = YubiKeyCapability::FIDO_U2F |
                       YubiKeyCapability::YubicoOTP |
                       YubiKeyCapability::OATH_HOTP |
                       YubiKeyCapability::OATH_TOTP |
                       YubiKeyCapability::PIV |
                       YubiKeyCapability::OpenPGP |
                       YubiKeyCapability::HMAC_SHA1;
        break;

    case YubiKeySeries::YubiKey4:
    case YubiKeySeries::YubiKey4FIPS:
        capabilities = YubiKeyCapability::FIDO_U2F |
                       YubiKeyCapability::YubicoOTP |
                       YubiKeyCapability::OATH_HOTP |
                       YubiKeyCapability::OATH_TOTP |
                       YubiKeyCapability::PIV |
                       YubiKeyCapability::OpenPGP |
                       YubiKeyCapability::HMAC_SHA1;
        break;

    default:
        break;
    }

    return createModel(series, variant, ports, capabilities);
}

} // namespace Shared
} // namespace YubiKeyOath
