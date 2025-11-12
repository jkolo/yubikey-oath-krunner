/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_icon_resolver.h"
#include "../types/device_brand.h"

namespace YubiKeyOath {
namespace Shared {

// Define static constant
const QString YubiKeyIconResolver::GENERIC_ICON_NAME = QStringLiteral("yubikey-oath");

QString YubiKeyIconResolver::getIconName(const DeviceModel& deviceModel)
{
    switch (deviceModel.brand) {
    case DeviceBrand::YubiKey:
        return getYubiKeyIconName(deviceModel.modelCode);
    case DeviceBrand::Nitrokey:
        return getNitrokeyIconName(deviceModel);
    case DeviceBrand::Unknown:
    default:
        return getGenericIconName();
    }
}

QString YubiKeyIconResolver::getIconName(YubiKeyModel model)
{
    // Legacy overload - delegate to brand-specific implementation
    return getYubiKeyIconName(model);
}

QString YubiKeyIconResolver::getNitrokeyIconName(const DeviceModel& deviceModel)
{
    // Convert model string to icon theme name: "Nitrokey 3C NFC" → "nitrokey-3c-nfc"
    QString iconName = deviceModel.modelString.toLower();
    iconName.replace(QLatin1Char(' '), QLatin1Char('-'));

    // Icon theme system will handle fallback automatically via QIcon::fromTheme():
    // 1. Try exact match (e.g., "nitrokey-3c-nfc")
    // 2. If not found, Qt looks for icon without variant suffix (e.g., "nitrokey-3c")
    // 3. If still not found, getIconName() caller provides "yubikey-oath" as generic fallback
    // Example: QIcon::fromTheme(iconName, QIcon::fromTheme("yubikey-oath"))
    //
    // This multi-level fallback ensures users always see an appropriate icon, even for
    // unknown device models or when icon files are missing from the theme installation.

    // Return the exact match - Qt's theme system handles the fallback chain
    return iconName;
}

QString YubiKeyIconResolver::getYubiKeyIconName(YubiKeyModel model)
{
    // Unknown model - return generic icon immediately
    if (model == 0) {
        return getGenericIconName();
    }

    // Extract model components
    const YubiKeySeries series = getModelSeries(model);
    const YubiKeyVariant variant = getModelVariant(model);
    const YubiKeyPorts ports = getModelPorts(model);

    // Special case: Series that don't support OATH applet
    // Bio and Security Key are FIDO-only devices
    if (series == YubiKeySeries::YubiKeyBio || series == YubiKeySeries::SecurityKey) {
        return getGenericIconName();
    }

    // Special case: YubiKey 5 USB-A only (no NFC, no variant)
    // We don't have a specific icon file for this configuration
    if ((series == YubiKeySeries::YubiKey5 || series == YubiKeySeries::YubiKey5FIPS) &&
        variant == YubiKeyVariant::Standard &&
        !ports.testFlag(YubiKeyPort::USB_C) &&
        !ports.testFlag(YubiKeyPort::NFC)) {
        return getGenericIconName();
    }

    // Icon theme system will automatically try:
    // Strategy 1: Exact match (series + variant + ports) - e.g., "yubikey-5c-nano"
    // Strategy 2: Series + ports (ignore variant) - e.g., "yubikey-5c-nfc"
    // Strategy 3: Generic fallback - "yubikey-oath"

    // For variant models (Nano, etc.), try exact match first
    if (variant != YubiKeyVariant::Standard) {
        return buildIconName(series, variant, ports, true);
    }

    // For standard models, use series + ports
    return buildIconName(series, variant, ports, false);
}

QString YubiKeyIconResolver::getGenericIconName()
{
    return GENERIC_ICON_NAME;
}

QString YubiKeyIconResolver::buildIconName(YubiKeySeries series,
                                                YubiKeyVariant variant,
                                                YubiKeyPorts ports,
                                                bool includeVariant)
{
    // Naming convention: yubikey-{series}{usb_type}[-{variant}][-nfc]
    // Examples:
    //   - yubikey-5-nfc (YubiKey 5 NFC, USB-A + NFC)
    //   - yubikey-5c-nfc (YubiKey 5C NFC, USB-C + NFC)
    //   - yubikey-5c-nano (YubiKey 5C Nano, USB-C + Nano variant)
    //   - yubikey-5ci (YubiKey 5Ci, USB-C + Lightning)

    QString filename = QStringLiteral("yubikey-");

    // 1. Add series (required)
    filename += seriesString(series);

    // 2. Add USB type (no hyphen, directly after series)
    const bool hasUSB_C = ports.testFlag(YubiKeyPort::USB_C);
    const bool hasLightning = ports.testFlag(YubiKeyPort::Lightning);
    const bool hasNFC = ports.testFlag(YubiKeyPort::NFC);

    // Special case: 5Ci (USB-C + Lightning)
    if (hasUSB_C && hasLightning) {
        filename += QStringLiteral("ci");
        return filename; // 5Ci has no variants or additional suffixes
    }

    // USB-C indicator (USB-A is default, no indicator)
    if (hasUSB_C) {
        filename += QStringLiteral("c");
    }

    // 3. Add variant (with hyphen) if requested and not Standard
    if (includeVariant && variant != YubiKeyVariant::Standard) {
        QString variantStr = variantString(variant);
        if (!variantStr.isEmpty()) {
            filename += QStringLiteral("-") + variantStr;
        }
    }

    // 4. Add NFC indicator (with hyphen)
    // Skip NFC suffix for NEO - all NEO models have NFC built-in by design
    if (hasNFC && series != YubiKeySeries::YubiKeyNEO) {
        filename += QStringLiteral("-nfc");
    }

    return filename;
}

QString YubiKeyIconResolver::seriesString(YubiKeySeries series)
{
    switch (series) {
        case YubiKeySeries::YubiKey5:
        case YubiKeySeries::YubiKey5FIPS:
            // FIPS models use same icons as non-FIPS counterparts
            return QStringLiteral("5");
        case YubiKeySeries::YubiKeyBio:
            // Bio series doesn't support OATH applet, but keep for completeness
            return QStringLiteral("bio");
        case YubiKeySeries::SecurityKey:
            // Security Key doesn't support OATH applet, but keep for completeness
            return QStringLiteral("security-key");
        case YubiKeySeries::YubiKeyNEO:
            return QStringLiteral("neo");
        case YubiKeySeries::YubiKey4:
        case YubiKeySeries::YubiKey4FIPS:
            // FIPS models use same icons as non-FIPS counterparts
            return QStringLiteral("4");
        case YubiKeySeries::Unknown:
        default:
            return QStringLiteral("unknown");
    }
}

QString YubiKeyIconResolver::variantString(YubiKeyVariant variant)
{
    switch (variant) {
        case YubiKeyVariant::Nano:
            return QStringLiteral("nano");
        case YubiKeyVariant::DualConnector:
            // Dual connector is represented in ports (USB-C + Lightning)
            // Don't add separate variant suffix
            [[fallthrough]];
        case YubiKeyVariant::EnhancedPIN:
            // Enhanced PIN doesn't change physical appearance
            [[fallthrough]];
        case YubiKeyVariant::Standard:
        default:
            return {};
    }
}

} // namespace Shared
} // namespace YubiKeyOath
