/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_icon_resolver.h"
#include "../types/device_brand.h"
#include <QFile>

namespace YubiKeyOath {
namespace Shared {

QString YubiKeyIconResolver::getIconPath(const DeviceModel& deviceModel)
{
    switch (deviceModel.brand) {
    case DeviceBrand::YubiKey:
        return getYubiKeyIconPath(deviceModel.modelCode);
    case DeviceBrand::Nitrokey:
        return getNitrokeyIconPath(deviceModel);
    case DeviceBrand::Unknown:
    default:
        return getGenericIconPath();
    }
}

QString YubiKeyIconResolver::getIconPath(YubiKeyModel model)
{
    // Legacy overload - delegate to brand-specific implementation
    return getYubiKeyIconPath(model);
}

QString YubiKeyIconResolver::getNitrokeyIconPath(const DeviceModel& deviceModel)
{
    // Convert model string to icon filename: "Nitrokey 3C NFC" → "nitrokey-3c-nfc"
    QString filename = deviceModel.modelString.toLower();
    filename.replace(QLatin1Char(' '), QLatin1Char('-'));

    // Strategy 1: Try exact match with NFC
    QString iconPath = buildResourcePath(filename);
    if (iconExists(iconPath)) {
        return iconPath;
    }

    // Strategy 2: Try without NFC suffix
    if (filename.endsWith(QStringLiteral("-nfc"))) {
        const QString filenameWithoutNFC = filename.left(filename.length() - 4);
        iconPath = buildResourcePath(filenameWithoutNFC);
        if (iconExists(iconPath)) {
            return iconPath;
        }
    }

    // Strategy 3: Generic fallback
    return getGenericIconPath();
}

QString YubiKeyIconResolver::getYubiKeyIconPath(YubiKeyModel model)
{
    // Unknown model - return generic icon immediately
    if (model == 0) {
        return getGenericIconPath();
    }

    // Extract model components
    const YubiKeySeries series = getModelSeries(model);
    const YubiKeyVariant variant = getModelVariant(model);
    const YubiKeyPorts ports = getModelPorts(model);

    // Strategy 1: Try exact match (series + variant + ports)
    if (variant != YubiKeyVariant::Standard) {
        const QString filename = buildIconFilename(series, variant, ports, true);
        QString iconPath = buildResourcePath(filename);
        if (iconExists(iconPath)) {
            return iconPath;
        }
    }

    // Strategy 2: Try series + ports (ignore variant)
    const QString filename = buildIconFilename(series, variant, ports, false);
    QString iconPath = buildResourcePath(filename);
    if (iconExists(iconPath)) {
        return iconPath;
    }

    // Strategy 3: Generic fallback
    // Note: There's no "YubiKey 5" or "YubiKey 5 FIPS" without specific variant/ports
    // All real models have concrete specifications (5 NFC, 5C, 5C NFC, etc.)
    return getGenericIconPath();
}

QString YubiKeyIconResolver::getGenericIconPath()
{
    return QLatin1String(GENERIC_ICON_PATH);
}

QString YubiKeyIconResolver::buildIconFilename(YubiKeySeries series,
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

bool YubiKeyIconResolver::iconExists(const QString& iconPath)
{
    return QFile::exists(iconPath);
}

QString YubiKeyIconResolver::buildResourcePath(const QString& filename)
{
    // Try PNG first (for real model images with transparency)
    QString pngPath = QLatin1String(ICON_PATH_PREFIX) + filename + QLatin1String(ICON_EXTENSION_PNG);
    if (QFile::exists(pngPath)) {
        return pngPath;
    }

    // Fall back to SVG (for legacy placeholders and series fallbacks)
    QString svgPath = QLatin1String(ICON_PATH_PREFIX) + filename + QLatin1String(ICON_EXTENSION_SVG);
    if (QFile::exists(svgPath)) {
        return svgPath;
    }

    // Return SVG path by default (will fail iconExists check later)
    return svgPath;
}

} // namespace Shared
} // namespace YubiKeyOath
